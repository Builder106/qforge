/* ============================================================================
 * pcap_parser.h — Nanosecond PCAP Binary Parser & Exchange Feed Replayer
 *
 * Implements a high-throughput, zero-dependency PCAP file parser and packet
 * replay engine with nanosecond timestamp precision for quantitative exchange
 * feed testing and market simulation.
 *
 * Key Capabilities:
 * 1. PCAP Binary Parsing:
 *    - Standard microsecond PCAP (0xa1b2c3d4) & nanosecond PCAP (0xa1b23c4d).
 *    - Dynamic endianness detection and on-the-fly conversion.
 *    - Memory-buffered and file streaming zero-copy packet reading.
 *    - Nanosecond PCAP packet capture writing and serialization.
 *
 * 2. Network Protocol Stack Decoding:
 *    - Layer 2: Ethernet II, IEEE 802.1Q VLAN tag unnesting.
 *    - Layer 3: IPv4 header parsing, subnet filtering, IP string conversion.
 *    - Layer 4: UDP and TCP header parsing, port filtering, payload extraction.
 *
 * 3. Market Data Feed Protocols:
 *    - NASDAQ ITCH 5.0 (Messages 'A', 'F', 'E', 'C', 'X', 'D', 'U', 'P').
 *    - MoldUDP64 multicast session protocol framing and multi-message extraction.
 *    - QForge Native Binary Feed Protocol (QFP) for zero-overhead direct replay.
 *    - Direct integration with L3 Limit Order Book (lob_book_t).
 *
 * 4. Nanosecond Packet Replay Engine:
 *    - As-Fast-As-Possible (AFAP / batch mode) line-rate replaying.
 *    - Real-time nanosecond paced replay with hybrid sleep + busy-spin timing.
 *    - Configurable speed scaling (e.g. 1.0x, 2.0x, 10.0x, 0.5x).
 *    - Fine-grained packet filtering by IP, port, protocol, and timestamp range.
 * ============================================================================ */

#ifndef PCAP_PARSER_H
#define PCAP_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "order_book.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * PCAP Magic Numbers & Constants
 * ---------------------------------------------------------------------------- */

#define PCAP_MAGIC_MICROSEC_NATIVE 0xa1b2c3d4U
#define PCAP_MAGIC_MICROSEC_SWAPPED 0xd4c3b2a1U
#define PCAP_MAGIC_NANOBSEC_NATIVE 0xa1b23c4dU
#define PCAP_MAGIC_NANOBSEC_SWAPPED 0x4d3cb2a1U

#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4

#define PCAP_LINKTYPE_NULL 0
#define PCAP_LINKTYPE_ETHERNET 1
#define PCAP_LINKTYPE_RAW 101
#define PCAP_LINKTYPE_LINUX_SLL 113

#define PCAP_MAX_PACKET_SIZE 65535

/* Protocol numbers (IPPROTO_*) */
#define PCAP_PROTO_TCP 6
#define PCAP_PROTO_UDP 17

/* EtherTypes */
#define PCAP_ETHERTYPE_IPV4 0x0800
#define PCAP_ETHERTYPE_VLAN 0x8100

/* ----------------------------------------------------------------------------
 * Result Codes
 * ---------------------------------------------------------------------------- */

typedef enum {
    PCAP_OK = 0,
    PCAP_ERR_INVALID_ARG = -1,
    PCAP_ERR_FILE_IO = -2,
    PCAP_ERR_BAD_MAGIC = -3,
    PCAP_ERR_UNSUPPORTED_VERSION = -4,
    PCAP_ERR_CORRUPT_HEADER = -5,
    PCAP_ERR_TRUNCATED_PACKET = -6,
    PCAP_ERR_EOF = -7,
    PCAP_ERR_BUFFER_OVERFLOW = -8,
    PCAP_ERR_UNSUPPORTED_LINKTYPE = -9,
    PCAP_ERR_PARSE_FAILED = -10
} pcap_result_t;

/* ----------------------------------------------------------------------------
 * PCAP File Header (24 bytes)
 * ---------------------------------------------------------------------------- */

typedef struct {
    uint32_t magic_number;  /* 0xa1b2c3d4 or 0xa1b23c4d */
    uint16_t version_major; /* 2 */
    uint16_t version_minor; /* 4 */
    int32_t thiszone;       /* GMT to local correction */
    uint32_t sigfigs;       /* accuracy of timestamps */
    uint32_t snaplen;       /* max length of captured packets */
    uint32_t network;       /* data link type (PCAP_LINKTYPE_*) */
} pcap_file_header_t;

/* ----------------------------------------------------------------------------
 * PCAP Packet Header (16 bytes)
 * ---------------------------------------------------------------------------- */

typedef struct {
    uint32_t ts_sec;   /* timestamp seconds */
    uint32_t ts_usec;  /* timestamp microseconds or nanoseconds */
    uint32_t incl_len; /* captured length in bytes */
    uint32_t orig_len; /* original length on wire */
} pcap_pkthdr_t;

/* ----------------------------------------------------------------------------
 * Normalized In-Memory Packet Representation
 * ---------------------------------------------------------------------------- */

typedef struct {
    uint64_t timestamp_ns; /* Unified absolute timestamp in nanoseconds */
    uint32_t incl_len;     /* Captured packet length */
    uint32_t orig_len;     /* Original wire length */
    const uint8_t *data;   /* Pointer into packet buffer */
} pcap_packet_t;

/* ----------------------------------------------------------------------------
 * Decoded Network Protocol Frame
 * ---------------------------------------------------------------------------- */

typedef struct {
    /* Layer 2: Ethernet */
    uint8_t src_mac[6];
    uint8_t dst_mac[6];
    uint16_t ethertype;
    uint16_t vlan_id; /* 0 if untagged, 1..4095 if 802.1Q tagged */
    bool has_vlan;

    /* Layer 3: IPv4 */
    bool has_ipv4;
    uint32_t src_ip;  /* Host byte order */
    uint32_t dst_ip;  /* Host byte order */
    uint8_t ip_proto; /* PCAP_PROTO_UDP or PCAP_PROTO_TCP */
    uint8_t ip_ttl;

    /* Layer 4: Transport (UDP or TCP) */
    uint16_t src_port; /* Host byte order */
    uint16_t dst_port; /* Host byte order */
    uint8_t tcp_flags; /* TCP flags if TCP */

    /* Transport Payload */
    const uint8_t *payload;
    size_t payload_len;
} pcap_frame_t;

/* ----------------------------------------------------------------------------
 * Market Data Feed Messages
 * ---------------------------------------------------------------------------- */

typedef enum {
    PCAP_FEED_MSG_ADD = 1,     /* Add new order to book */
    PCAP_FEED_MSG_EXECUTE = 2, /* Partial or full execution of resting order */
    PCAP_FEED_MSG_CANCEL = 3,  /* Reduce resting order quantity */
    PCAP_FEED_MSG_DELETE = 4,  /* Delete resting order completely */
    PCAP_FEED_MSG_REPLACE = 5, /* Replace/modify resting order */
    PCAP_FEED_MSG_TRADE = 6    /* Non-book crossing trade execution */
} pcap_feed_msg_type_t;

typedef struct {
    pcap_feed_msg_type_t type;
    uint64_t timestamp_ns; /* Nanosecond timestamp */
    uint64_t order_id;     /* Order reference number */
    uint64_t new_order_id; /* New order ID for replace/modify */
    uint64_t price;        /* Price in ticks */
    uint32_t qty;          /* Quantity / shares */
    uint32_t executed_qty; /* Executed shares for EXECUTE */
    uint64_t match_id;     /* Execution/trade match ID */
    uint8_t side;          /* LOB_SIDE_BUY or LOB_SIDE_SELL */
    char symbol[9];        /* Stock symbol null-terminated */
} pcap_feed_msg_t;

typedef void (*pcap_feed_msg_callback_t)(const pcap_feed_msg_t *msg, void *user_data);

/* ----------------------------------------------------------------------------
 * MoldUDP64 Multicast Header (20 bytes)
 * ---------------------------------------------------------------------------- */

typedef struct {
    char session[10];   /* Session name */
    uint64_t seq_num;   /* Sequence number (start of block) */
    uint16_t msg_count; /* Number of bundled messages */
} pcap_moldudp64_hdr_t;

/* ----------------------------------------------------------------------------
 * QForge Native Packet Protocol (QFP) (32 bytes)
 * Zero-overhead direct serialization for L3 Order Book simulation
 * ---------------------------------------------------------------------------- */

typedef struct {
    uint8_t msg_type;   /* 1=ADD, 2=CANCEL, 3=MODIFY, 4=EXECUTE */
    uint8_t side;       /* 0=BUY, 1=SELL */
    uint8_t order_type; /* lob_type_t (0=LIMIT, 1=IOC, 2=FOK, 3=POST_ONLY, 4=MARKET) */
    uint8_t _pad0;
    uint32_t qty;          /* Shares / contracts */
    uint64_t price;        /* Price in ticks */
    uint64_t order_id;     /* Unique 64-bit order ID */
    uint64_t timestamp_ns; /* Nanosecond timestamp */
} pcap_qfp_msg_t;

/* ----------------------------------------------------------------------------
 * PCAP Reader
 * ---------------------------------------------------------------------------- */

typedef struct pcap_reader pcap_reader_t;

/* Open PCAP file from filesystem for reading */
pcap_reader_t *pcap_reader_open(const char *filename);

/* Open PCAP parser from an in-memory buffer (zero-copy) */
pcap_reader_t *pcap_reader_open_memory(const void *buf, size_t size);

/* Close reader and free internal state */
void pcap_reader_close(pcap_reader_t *reader);

/* Reset reader to beginning of packet stream */
pcap_result_t pcap_reader_rewind(pcap_reader_t *reader);

/* Get file-level header metadata */
const pcap_file_header_t *pcap_reader_get_header(const pcap_reader_t *reader);

/* Check if PCAP uses nanosecond timestamp resolution */
bool pcap_reader_is_nanosecond(const pcap_reader_t *reader);

/* Check if PCAP was captured with swapped byte order */
bool pcap_reader_is_swapped(const pcap_reader_t *reader);

/*
 * Read next packet from capture stream.
 * out_pkt: filled with timestamp and pointer to packet data.
 * Returns PCAP_OK, PCAP_ERR_EOF at end of file, or negative error code.
 */
pcap_result_t pcap_reader_next_packet(pcap_reader_t *reader, pcap_packet_t *out_pkt);

/* ----------------------------------------------------------------------------
 * PCAP Writer
 * ---------------------------------------------------------------------------- */

typedef struct pcap_writer pcap_writer_t;

/*
 * Create a new PCAP capture file.
 * nanosecond: true for nanosecond magic (0xa1b23c4d), false for microsecond.
 * linktype: PCAP_LINKTYPE_* (default PCAP_LINKTYPE_ETHERNET).
 */
pcap_writer_t *pcap_writer_create(const char *filename, bool nanosecond, uint32_t linktype);

/* Write a raw packet with nanosecond timestamp to PCAP file */
pcap_result_t pcap_writer_write_packet(pcap_writer_t *writer, uint64_t timestamp_ns,
                                       const void *data, uint32_t len);

/* Flush and close writer */
void pcap_writer_close(pcap_writer_t *writer);

/* ----------------------------------------------------------------------------
 * Protocol Decoding & Helpers
 * ---------------------------------------------------------------------------- */

/*
 * Decode network frame (Ethernet, VLAN, IPv4, UDP/TCP).
 * packet_data: raw packet bytes.
 * packet_len: packet length.
 * linktype: PCAP_LINKTYPE_*.
 * out_frame: populated decoded metadata.
 */
pcap_result_t pcap_decode_frame(const uint8_t *packet_data, uint32_t packet_len, uint32_t linktype,
                                pcap_frame_t *out_frame);

/* Format 32-bit IPv4 address (host byte order) to "a.b.c.d" string */
void pcap_ip_to_str(uint32_t ip, char *buf, size_t buf_len);

/* Parse "a.b.c.d" string into 32-bit IPv4 address (host byte order) */
bool pcap_str_to_ip(const char *str, uint32_t *out_ip);

/* Format 6-byte MAC address to "xx:xx:xx:xx:xx:xx" string */
void pcap_mac_to_str(const uint8_t mac[6], char *buf, size_t buf_len);

/* ----------------------------------------------------------------------------
 * Market Data Feed Parsers & Encoders
 * ---------------------------------------------------------------------------- */

/*
 * Parse a single NASDAQ ITCH 5.0 binary message.
 * Returns PCAP_OK if parsed successfully, or PCAP_ERR_PARSE_FAILED.
 */
pcap_result_t pcap_parse_itch_msg(const uint8_t *data, size_t len, pcap_feed_msg_t *out_msg);

/*
 * Parse a MoldUDP64 packet payload containing one or more ITCH messages.
 * Invokes callback for each decoded feed message.
 */
pcap_result_t pcap_parse_moldudp64(const uint8_t *data, size_t len, pcap_feed_msg_callback_t on_msg,
                                   void *user_data);

/*
 * Parse a QFP (QForge Packet) message.
 */
pcap_result_t pcap_parse_qfp(const uint8_t *data, size_t len, pcap_feed_msg_t *out_msg);

/*
 * Apply a decoded feed message directly to an L3 limit order book.
 * Dispatches ADD, CANCEL, EXECUTE, DELETE, REPLACE directly to lob_book_t.
 */
lob_result_t pcap_feed_apply_to_book(lob_book_t *book, const pcap_feed_msg_t *msg);

/* Packet Construction & Encoding Helpers */
size_t pcap_encode_ethernet(uint8_t *buf, size_t max_len, const uint8_t dst_mac[6],
                            const uint8_t src_mac[6], uint16_t ethertype);

size_t pcap_encode_ipv4(uint8_t *buf, size_t max_len, uint32_t src_ip, uint32_t dst_ip,
                        uint8_t proto, uint16_t payload_len);

size_t pcap_encode_udp(uint8_t *buf, size_t max_len, uint16_t src_port, uint16_t dst_port,
                       uint16_t payload_len);

size_t pcap_encode_moldudp64(uint8_t *buf, size_t max_len, const char session[10], uint64_t seq_num,
                             uint16_t msg_count);

size_t pcap_encode_itch_add(uint8_t *buf, size_t max_len, uint64_t timestamp_ns, uint64_t order_id,
                            uint8_t side, uint32_t shares, const char *symbol,
                            uint32_t price_fixed);

size_t pcap_encode_itch_execute(uint8_t *buf, size_t max_len, uint64_t timestamp_ns,
                                uint64_t order_id, uint32_t shares, uint64_t match_id);

size_t pcap_encode_itch_cancel(uint8_t *buf, size_t max_len, uint64_t timestamp_ns,
                               uint64_t order_id, uint32_t shares);

size_t pcap_encode_itch_delete(uint8_t *buf, size_t max_len, uint64_t timestamp_ns,
                               uint64_t order_id);

size_t pcap_encode_itch_replace(uint8_t *buf, size_t max_len, uint64_t timestamp_ns,
                                uint64_t orig_order_id, uint64_t new_order_id, uint32_t shares,
                                uint32_t price_fixed);

size_t pcap_encode_qfp(uint8_t *buf, size_t max_len, const pcap_qfp_msg_t *msg);

/* ----------------------------------------------------------------------------
 * Nanosecond Packet Replay Engine
 * ---------------------------------------------------------------------------- */

typedef struct {
    /*
     * Replay speed multiplier:
     *   0.0: As-Fast-As-Possible (AFAP / line rate / batch mode)
     *   1.0: Real-time 1:1 playback
     *   2.0: 2x fast-forward
     *   0.5: Half-speed playback
     */
    double speed_multiplier;

    /* Filtering options (0 / false = no filter) */
    uint32_t filter_src_ip;
    uint32_t filter_dst_ip;
    uint32_t filter_ip_mask;
    uint16_t filter_src_port;
    uint16_t filter_dst_port;
    uint8_t filter_proto;   /* PCAP_PROTO_UDP, PCAP_PROTO_TCP, or 0 */
    uint64_t start_time_ns; /* 0 = beginning of file */
    uint64_t end_time_ns;   /* UINT64_MAX = end of file */

    /* Packet handler callbacks */
    void (*on_packet)(const pcap_packet_t *pkt, void *user_data);
    void (*on_frame)(const pcap_frame_t *frame, const pcap_packet_t *pkt, void *user_data);
    void (*on_feed_msg)(const pcap_feed_msg_t *msg, void *user_data);
    void *user_data;
} pcap_replay_config_t;

typedef struct {
    uint64_t total_packets;      /* Total packets read from capture */
    uint64_t matched_packets;    /* Packets passing filter rules */
    uint64_t total_bytes;        /* Total bytes processed */
    uint64_t feed_messages;      /* Market data messages parsed */
    uint64_t start_timestamp_ns; /* First packet capture timestamp */
    uint64_t end_timestamp_ns;   /* Last packet capture timestamp */
    uint64_t elapsed_wall_ns;    /* Real wall-clock elapsed nanoseconds */
    double packets_per_sec;      /* Replay throughput in packets/sec */
    double mbps;                 /* Bandwidth in Megabits/sec */
} pcap_replay_stats_t;

typedef struct pcap_replayer pcap_replayer_t;

/* Create a packet capture replayer */
pcap_replayer_t *pcap_replayer_create(pcap_reader_t *reader, const pcap_replay_config_t *config);

/* Free replayer */
void pcap_replayer_free(pcap_replayer_t *replayer);

/*
 * Step one packet through the replayer (with pacing if configured).
 * Returns PCAP_OK, PCAP_ERR_EOF when complete, or negative error code.
 */
pcap_result_t pcap_replayer_step(pcap_replayer_t *replayer, pcap_packet_t *out_pkt);

/*
 * Run replayer to completion (or until error).
 * Returns PCAP_OK or negative error code.
 */
pcap_result_t pcap_replayer_run(pcap_replayer_t *replayer);

/* Query replay statistics */
void pcap_replayer_get_stats(const pcap_replayer_t *replayer, pcap_replay_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* PCAP_PARSER_H */
