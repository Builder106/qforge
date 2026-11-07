/* ============================================================================
 * pcap_parser.c — Nanosecond PCAP Binary Parser & Exchange Feed Replayer
 *
 * Zero-dependency C99 implementation of PCAP parser and replay engine.
 * ============================================================================ */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "pcap_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ----------------------------------------------------------------------------
 * Endianness Detection & Byte Swapping Helpers
 * ---------------------------------------------------------------------------- */

static inline bool pcap_is_little_endian(void) {
    uint16_t x = 0x0001;
    return *((const uint8_t *)&x) == 0x01;
}

static inline uint16_t pcap_bswap16(uint16_t val) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap16(val);
#else
    return (uint16_t)((val << 8) | (val >> 8));
#endif
}

static inline uint32_t pcap_bswap32(uint32_t val) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(val);
#else
    return ((val << 24) & 0xff000000U) | ((val << 8) & 0x00ff0000U) | ((val >> 8) & 0x0000ff00U) |
           ((val >> 24) & 0x000000ffU);
#endif
}

static inline uint64_t pcap_bswap64(uint64_t val) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(val);
#else
    return (((uint64_t)pcap_bswap32((uint32_t)(val & 0xffffffffULL))) << 32) |
           ((uint64_t)pcap_bswap32((uint32_t)(val >> 32)));
#endif
}

#if defined(__GNUC__) || defined(__clang__)
#define PCAP_UNUSED __attribute__((unused))
#else
#define PCAP_UNUSED
#endif

static inline PCAP_UNUSED uint16_t pcap_be16(uint16_t val) {
    return pcap_is_little_endian() ? pcap_bswap16(val) : val;
}

static inline PCAP_UNUSED uint32_t pcap_be32(uint32_t val) {
    return pcap_is_little_endian() ? pcap_bswap32(val) : val;
}

static inline PCAP_UNUSED uint64_t pcap_be64(uint64_t val) {
    return pcap_is_little_endian() ? pcap_bswap64(val) : val;
}

static inline PCAP_UNUSED uint16_t pcap_le16(uint16_t val) {
    return pcap_is_little_endian() ? val : pcap_bswap16(val);
}

static inline PCAP_UNUSED uint64_t pcap_le64(uint64_t val) {
    return pcap_is_little_endian() ? val : pcap_bswap64(val);
}

static inline uint16_t pcap_read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t pcap_read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint64_t pcap_read_be48(const uint8_t *p) {
    return ((uint64_t)p[0] << 40) | ((uint64_t)p[1] << 32) | ((uint64_t)p[2] << 24) |
           ((uint64_t)p[3] << 16) | ((uint64_t)p[4] << 8) | (uint64_t)p[5];
}

static inline uint64_t pcap_read_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

static inline uint16_t pcap_read_le16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}

static inline PCAP_UNUSED uint64_t pcap_read_le64(const uint8_t *p) {
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static inline void pcap_write_be16(uint8_t *p, uint16_t val) {
    p[0] = (uint8_t)((val >> 8) & 0xff);
    p[1] = (uint8_t)(val & 0xff);
}

static inline void pcap_write_be32(uint8_t *p, uint32_t val) {
    p[0] = (uint8_t)((val >> 24) & 0xff);
    p[1] = (uint8_t)((val >> 16) & 0xff);
    p[2] = (uint8_t)((val >> 8) & 0xff);
    p[3] = (uint8_t)(val & 0xff);
}

static inline void pcap_write_be48(uint8_t *p, uint64_t val) {
    p[0] = (uint8_t)((val >> 40) & 0xff);
    p[1] = (uint8_t)((val >> 32) & 0xff);
    p[2] = (uint8_t)((val >> 24) & 0xff);
    p[3] = (uint8_t)((val >> 16) & 0xff);
    p[4] = (uint8_t)((val >> 8) & 0xff);
    p[5] = (uint8_t)(val & 0xff);
}

static inline void pcap_write_be64(uint8_t *p, uint64_t val) {
    p[0] = (uint8_t)((val >> 56) & 0xff);
    p[1] = (uint8_t)((val >> 48) & 0xff);
    p[2] = (uint8_t)((val >> 40) & 0xff);
    p[3] = (uint8_t)((val >> 32) & 0xff);
    p[4] = (uint8_t)((val >> 24) & 0xff);
    p[5] = (uint8_t)((val >> 16) & 0xff);
    p[6] = (uint8_t)((val >> 8) & 0xff);
    p[7] = (uint8_t)(val & 0xff);
}

static inline void pcap_write_le16(uint8_t *p, uint16_t val) {
    p[0] = (uint8_t)(val & 0xff);
    p[1] = (uint8_t)((val >> 8) & 0xff);
}

static inline void pcap_write_le64(uint8_t *p, uint64_t val) {
    p[0] = (uint8_t)(val & 0xff);
    p[1] = (uint8_t)((val >> 8) & 0xff);
    p[2] = (uint8_t)((val >> 16) & 0xff);
    p[3] = (uint8_t)((val >> 24) & 0xff);
    p[4] = (uint8_t)((val >> 32) & 0xff);
    p[5] = (uint8_t)((val >> 40) & 0xff);
    p[6] = (uint8_t)((val >> 48) & 0xff);
    p[7] = (uint8_t)((val >> 56) & 0xff);
}

static inline uint64_t pcap_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

/* ----------------------------------------------------------------------------
 * PCAP Reader Implementation
 * ---------------------------------------------------------------------------- */

struct pcap_reader {
    FILE *fp;
    const uint8_t *mem_buf;
    size_t mem_size;
    size_t mem_offset;
    pcap_file_header_t header;
    bool is_nanosec;
    bool is_swapped;
    bool is_memory;
    uint8_t packet_buf[PCAP_MAX_PACKET_SIZE];
};

static pcap_result_t pcap_parse_global_header(pcap_reader_t *reader, const uint8_t raw[24]) {
    uint32_t magic = *((const uint32_t *)&raw[0]);

    if (magic == PCAP_MAGIC_MICROSEC_NATIVE) {
        reader->is_swapped = false;
        reader->is_nanosec = false;
    } else if (magic == PCAP_MAGIC_MICROSEC_SWAPPED) {
        reader->is_swapped = true;
        reader->is_nanosec = false;
    } else if (magic == PCAP_MAGIC_NANOBSEC_NATIVE) {
        reader->is_swapped = false;
        reader->is_nanosec = true;
    } else if (magic == PCAP_MAGIC_NANOBSEC_SWAPPED) {
        reader->is_swapped = true;
        reader->is_nanosec = true;
    } else {
        return PCAP_ERR_BAD_MAGIC;
    }

    reader->header.magic_number = magic;
    if (!reader->is_swapped) {
        reader->header.version_major = *((const uint16_t *)&raw[4]);
        reader->header.version_minor = *((const uint16_t *)&raw[6]);
        reader->header.thiszone = *((const int32_t *)&raw[8]);
        reader->header.sigfigs = *((const uint32_t *)&raw[12]);
        reader->header.snaplen = *((const uint32_t *)&raw[16]);
        reader->header.network = *((const uint32_t *)&raw[20]);
    } else {
        reader->header.version_major = pcap_bswap16(*((const uint16_t *)&raw[4]));
        reader->header.version_minor = pcap_bswap16(*((const uint16_t *)&raw[6]));
        reader->header.thiszone = (int32_t)pcap_bswap32(*((const uint32_t *)&raw[8]));
        reader->header.sigfigs = pcap_bswap32(*((const uint32_t *)&raw[12]));
        reader->header.snaplen = pcap_bswap32(*((const uint32_t *)&raw[16]));
        reader->header.network = pcap_bswap32(*((const uint32_t *)&raw[20]));
    }

    if (reader->header.version_major != PCAP_VERSION_MAJOR) {
        return PCAP_ERR_UNSUPPORTED_VERSION;
    }

    return PCAP_OK;
}

pcap_reader_t *pcap_reader_open(const char *filename) {
    if (!filename) {
        return NULL;
    }

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        return NULL;
    }

    pcap_reader_t *reader = (pcap_reader_t *)calloc(1, sizeof(pcap_reader_t));
    if (!reader) {
        fclose(fp);
        return NULL;
    }

    reader->fp = fp;
    reader->is_memory = false;

    uint8_t header_bytes[24];
    if (fread(header_bytes, 1, 24, fp) != 24) {
        fclose(fp);
        free(reader);
        return NULL;
    }

    if (pcap_parse_global_header(reader, header_bytes) != PCAP_OK) {
        fclose(fp);
        free(reader);
        return NULL;
    }

    return reader;
}

pcap_reader_t *pcap_reader_open_memory(const void *buf, size_t size) {
    if (!buf || size < 24) {
        return NULL;
    }

    pcap_reader_t *reader = (pcap_reader_t *)calloc(1, sizeof(pcap_reader_t));
    if (!reader) {
        return NULL;
    }

    reader->mem_buf = (const uint8_t *)buf;
    reader->mem_size = size;
    reader->mem_offset = 24;
    reader->is_memory = true;

    if (pcap_parse_global_header(reader, (const uint8_t *)buf) != PCAP_OK) {
        free(reader);
        return NULL;
    }

    return reader;
}

void pcap_reader_close(pcap_reader_t *reader) {
    if (!reader) {
        return;
    }
    if (!reader->is_memory && reader->fp) {
        fclose(reader->fp);
        reader->fp = NULL;
    }
    free(reader);
}

pcap_result_t pcap_reader_rewind(pcap_reader_t *reader) {
    if (!reader) {
        return PCAP_ERR_INVALID_ARG;
    }
    if (reader->is_memory) {
        reader->mem_offset = 24;
        return PCAP_OK;
    }
    if (!reader->fp) {
        return PCAP_ERR_FILE_IO;
    }
    if (fseek(reader->fp, 24, SEEK_SET) != 0) {
        return PCAP_ERR_FILE_IO;
    }
    return PCAP_OK;
}

const pcap_file_header_t *pcap_reader_get_header(const pcap_reader_t *reader) {
    return reader ? &reader->header : NULL;
}

bool pcap_reader_is_nanosecond(const pcap_reader_t *reader) {
    return reader ? reader->is_nanosec : false;
}

bool pcap_reader_is_swapped(const pcap_reader_t *reader) {
    return reader ? reader->is_swapped : false;
}

pcap_result_t pcap_reader_next_packet(pcap_reader_t *reader, pcap_packet_t *out_pkt) {
    if (!reader || !out_pkt) {
        return PCAP_ERR_INVALID_ARG;
    }

    uint8_t hdr_bytes[16];
    uint32_t ts_sec, ts_usec, incl_len, orig_len;

    if (reader->is_memory) {
        if (reader->mem_offset + 16 > reader->mem_size) {
            return PCAP_ERR_EOF;
        }
        memcpy(hdr_bytes, reader->mem_buf + reader->mem_offset, 16);
        reader->mem_offset += 16;
    } else {
        size_t n = fread(hdr_bytes, 1, 16, reader->fp);
        if (n == 0) {
            return PCAP_ERR_EOF;
        }
        if (n < 16) {
            return PCAP_ERR_TRUNCATED_PACKET;
        }
    }

    if (!reader->is_swapped) {
        ts_sec = *((const uint32_t *)&hdr_bytes[0]);
        ts_usec = *((const uint32_t *)&hdr_bytes[4]);
        incl_len = *((const uint32_t *)&hdr_bytes[8]);
        orig_len = *((const uint32_t *)&hdr_bytes[12]);
    } else {
        ts_sec = pcap_bswap32(*((const uint32_t *)&hdr_bytes[0]));
        ts_usec = pcap_bswap32(*((const uint32_t *)&hdr_bytes[4]));
        incl_len = pcap_bswap32(*((const uint32_t *)&hdr_bytes[8]));
        orig_len = pcap_bswap32(*((const uint32_t *)&hdr_bytes[12]));
    }

    if (incl_len > PCAP_MAX_PACKET_SIZE) {
        return PCAP_ERR_BUFFER_OVERFLOW;
    }

    if (reader->is_memory) {
        if (reader->mem_offset + incl_len > reader->mem_size) {
            return PCAP_ERR_TRUNCATED_PACKET;
        }
        out_pkt->data = reader->mem_buf + reader->mem_offset;
        reader->mem_offset += incl_len;
    } else {
        size_t n = fread(reader->packet_buf, 1, incl_len, reader->fp);
        if (n < incl_len) {
            return PCAP_ERR_TRUNCATED_PACKET;
        }
        out_pkt->data = reader->packet_buf;
    }

    out_pkt->incl_len = incl_len;
    out_pkt->orig_len = orig_len;

    /* Normalize to nanoseconds */
    if (reader->is_nanosec) {
        out_pkt->timestamp_ns = ((uint64_t)ts_sec * 1000000000ULL) + (uint64_t)ts_usec;
    } else {
        out_pkt->timestamp_ns = ((uint64_t)ts_sec * 1000000000ULL) + ((uint64_t)ts_usec * 1000ULL);
    }

    return PCAP_OK;
}

/* ----------------------------------------------------------------------------
 * PCAP Writer Implementation
 * ---------------------------------------------------------------------------- */

struct pcap_writer {
    FILE *fp;
    bool is_nanosec;
    uint32_t linktype;
    uint32_t snaplen;
};

pcap_writer_t *pcap_writer_create(const char *filename, bool nanosecond, uint32_t linktype) {
    if (!filename) {
        return NULL;
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        return NULL;
    }

    pcap_writer_t *writer = (pcap_writer_t *)calloc(1, sizeof(pcap_writer_t));
    if (!writer) {
        fclose(fp);
        return NULL;
    }

    writer->fp = fp;
    writer->is_nanosec = nanosecond;
    writer->linktype = linktype ? linktype : PCAP_LINKTYPE_ETHERNET;
    writer->snaplen = PCAP_MAX_PACKET_SIZE;

    uint8_t header_bytes[24];
    uint32_t magic = nanosecond ? PCAP_MAGIC_NANOBSEC_NATIVE : PCAP_MAGIC_MICROSEC_NATIVE;
    uint16_t v_maj = PCAP_VERSION_MAJOR;
    uint16_t v_min = PCAP_VERSION_MINOR;
    int32_t tz = 0;
    uint32_t sigfigs = 0;

    memcpy(&header_bytes[0], &magic, 4);
    memcpy(&header_bytes[4], &v_maj, 2);
    memcpy(&header_bytes[6], &v_min, 2);
    memcpy(&header_bytes[8], &tz, 4);
    memcpy(&header_bytes[12], &sigfigs, 4);
    memcpy(&header_bytes[16], &writer->snaplen, 4);
    memcpy(&header_bytes[20], &writer->linktype, 4);

    if (fwrite(header_bytes, 1, 24, fp) != 24) {
        fclose(fp);
        free(writer);
        return NULL;
    }

    return writer;
}

pcap_result_t pcap_writer_write_packet(pcap_writer_t *writer, uint64_t timestamp_ns,
                                       const void *data, uint32_t len) {
    if (!writer || !writer->fp || !data) {
        return PCAP_ERR_INVALID_ARG;
    }

    uint32_t incl_len = len > writer->snaplen ? writer->snaplen : len;
    uint32_t orig_len = len;
    uint32_t ts_sec = (uint32_t)(timestamp_ns / 1000000000ULL);
    uint32_t ts_usec;

    if (writer->is_nanosec) {
        ts_usec = (uint32_t)(timestamp_ns % 1000000000ULL);
    } else {
        ts_usec = (uint32_t)((timestamp_ns % 1000000000ULL) / 1000ULL);
    }

    uint8_t hdr_bytes[16];
    memcpy(&hdr_bytes[0], &ts_sec, 4);
    memcpy(&hdr_bytes[4], &ts_usec, 4);
    memcpy(&hdr_bytes[8], &incl_len, 4);
    memcpy(&hdr_bytes[12], &orig_len, 4);

    if (fwrite(hdr_bytes, 1, 16, writer->fp) != 16) {
        return PCAP_ERR_FILE_IO;
    }

    if (fwrite(data, 1, incl_len, writer->fp) != incl_len) {
        return PCAP_ERR_FILE_IO;
    }

    return PCAP_OK;
}

void pcap_writer_close(pcap_writer_t *writer) {
    if (!writer) {
        return;
    }
    if (writer->fp) {
        fflush(writer->fp);
        fclose(writer->fp);
        writer->fp = NULL;
    }
    free(writer);
}

/* ----------------------------------------------------------------------------
 * Protocol Decoding & Helpers
 * ---------------------------------------------------------------------------- */

pcap_result_t pcap_decode_frame(const uint8_t *packet_data, uint32_t packet_len, uint32_t linktype,
                                pcap_frame_t *out_frame) {
    if (!packet_data || !out_frame) {
        return PCAP_ERR_INVALID_ARG;
    }

    memset(out_frame, 0, sizeof(pcap_frame_t));
    const uint8_t *ptr = packet_data;
    uint32_t rem = packet_len;

    if (linktype == PCAP_LINKTYPE_ETHERNET) {
        if (rem < 14) {
            return PCAP_ERR_TRUNCATED_PACKET;
        }

        memcpy(out_frame->dst_mac, ptr, 6);
        memcpy(out_frame->src_mac, ptr + 6, 6);
        out_frame->ethertype = pcap_read_be16(ptr + 12);
        ptr += 14;
        rem -= 14;

        /* IEEE 802.1Q VLAN Tagging */
        if (out_frame->ethertype == PCAP_ETHERTYPE_VLAN) {
            if (rem < 4) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            uint16_t tci = pcap_read_be16(ptr);
            out_frame->vlan_id = tci & 0x0FFF;
            out_frame->has_vlan = true;
            out_frame->ethertype = pcap_read_be16(ptr + 2);
            ptr += 4;
            rem -= 4;
        }
    } else if (linktype == PCAP_LINKTYPE_RAW) {
        /* Raw IP packet */
        out_frame->ethertype = PCAP_ETHERTYPE_IPV4;
    } else if (linktype == PCAP_LINKTYPE_LINUX_SLL) {
        if (rem < 16) {
            return PCAP_ERR_TRUNCATED_PACKET;
        }
        out_frame->ethertype = pcap_read_be16(ptr + 14);
        ptr += 16;
        rem -= 16;
    } else if (linktype == PCAP_LINKTYPE_NULL) {
        if (rem < 4) {
            return PCAP_ERR_TRUNCATED_PACKET;
        }
        ptr += 4;
        rem -= 4;
        out_frame->ethertype = PCAP_ETHERTYPE_IPV4;
    } else {
        return PCAP_ERR_UNSUPPORTED_LINKTYPE;
    }

    /* Layer 3: IPv4 */
    if (out_frame->ethertype == PCAP_ETHERTYPE_IPV4) {
        if (rem < 20) {
            return PCAP_ERR_TRUNCATED_PACKET;
        }

        uint8_t ver_ihl = ptr[0];
        uint8_t version = (ver_ihl >> 4) & 0x0F;
        uint8_t ihl = (ver_ihl & 0x0F) * 4;

        if (version != 4 || ihl < 20 || ihl > rem) {
            return PCAP_ERR_CORRUPT_HEADER;
        }

        out_frame->has_ipv4 = true;
        out_frame->ip_ttl = ptr[8];
        out_frame->ip_proto = ptr[9];
        out_frame->src_ip = pcap_read_be32(ptr + 12);
        out_frame->dst_ip = pcap_read_be32(ptr + 16);

        ptr += ihl;
        rem -= ihl;

        /* Layer 4: Transport (UDP or TCP) */
        if (out_frame->ip_proto == PCAP_PROTO_UDP) {
            if (rem < 8) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_frame->src_port = pcap_read_be16(ptr);
            out_frame->dst_port = pcap_read_be16(ptr + 2);
            uint16_t udp_len = pcap_read_be16(ptr + 4);

            ptr += 8;
            rem -= 8;

            size_t actual_payload = (udp_len >= 8) ? (size_t)(udp_len - 8) : 0;
            if (actual_payload > rem) {
                actual_payload = rem;
            }
            out_frame->payload = ptr;
            out_frame->payload_len = actual_payload;
            return PCAP_OK;
        } else if (out_frame->ip_proto == PCAP_PROTO_TCP) {
            if (rem < 20) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_frame->src_port = pcap_read_be16(ptr);
            out_frame->dst_port = pcap_read_be16(ptr + 2);
            uint8_t data_offset = ((ptr[12] >> 4) & 0x0F) * 4;
            out_frame->tcp_flags = ptr[13];

            if (data_offset < 20 || data_offset > rem) {
                return PCAP_ERR_CORRUPT_HEADER;
            }

            ptr += data_offset;
            rem -= data_offset;

            out_frame->payload = ptr;
            out_frame->payload_len = rem;
            return PCAP_OK;
        }
    }

    out_frame->payload = ptr;
    out_frame->payload_len = rem;
    return PCAP_OK;
}

void pcap_ip_to_str(uint32_t ip, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return;
    }
    snprintf(buf, buf_len, "%u.%u.%u.%u", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff,
             ip & 0xff);
}

bool pcap_str_to_ip(const char *str, uint32_t *out_ip) {
    if (!str || !out_ip) {
        return false;
    }
    unsigned int a, b, c, d;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return false;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return false;
    }
    *out_ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
    return true;
}

void pcap_mac_to_str(const uint8_t mac[6], char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return;
    }
    snprintf(buf, buf_len, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
}

/* ----------------------------------------------------------------------------
 * Market Data Feed Parsers
 * ---------------------------------------------------------------------------- */

pcap_result_t pcap_parse_itch_msg(const uint8_t *data, size_t len, pcap_feed_msg_t *out_msg) {
    if (!data || !out_msg || len == 0) {
        return PCAP_ERR_INVALID_ARG;
    }

    memset(out_msg, 0, sizeof(pcap_feed_msg_t));
    char msg_type = (char)data[0];

    switch (msg_type) {
        case 'A': { /* Add Order (36 bytes) */
            if (len < 36) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_msg->type = PCAP_FEED_MSG_ADD;
            out_msg->timestamp_ns = pcap_read_be48(data + 5);
            out_msg->order_id = pcap_read_be64(data + 11);
            char side_ch = (char)data[19];
            out_msg->side = (side_ch == 'B') ? LOB_SIDE_BUY : LOB_SIDE_SELL;
            out_msg->qty = pcap_read_be32(data + 20);
            memcpy(out_msg->symbol, data + 24, 8);
            out_msg->symbol[8] = '\0';
            /* Trim trailing spaces */
            for (int i = 7; i >= 0 && out_msg->symbol[i] == ' '; i--) {
                out_msg->symbol[i] = '\0';
            }
            out_msg->price = (uint64_t)pcap_read_be32(data + 32);
            return PCAP_OK;
        }
        case 'F': { /* Add Order with MPID (40 bytes) */
            if (len < 40) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_msg->type = PCAP_FEED_MSG_ADD;
            out_msg->timestamp_ns = pcap_read_be48(data + 5);
            out_msg->order_id = pcap_read_be64(data + 11);
            char side_ch = (char)data[19];
            out_msg->side = (side_ch == 'B') ? LOB_SIDE_BUY : LOB_SIDE_SELL;
            out_msg->qty = pcap_read_be32(data + 20);
            memcpy(out_msg->symbol, data + 24, 8);
            out_msg->symbol[8] = '\0';
            for (int i = 7; i >= 0 && out_msg->symbol[i] == ' '; i--) {
                out_msg->symbol[i] = '\0';
            }
            out_msg->price = (uint64_t)pcap_read_be32(data + 32);
            return PCAP_OK;
        }
        case 'E': { /* Order Executed (31 bytes) */
            if (len < 31) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_msg->type = PCAP_FEED_MSG_EXECUTE;
            out_msg->timestamp_ns = pcap_read_be48(data + 5);
            out_msg->order_id = pcap_read_be64(data + 11);
            out_msg->executed_qty = pcap_read_be32(data + 19);
            out_msg->match_id = pcap_read_be64(data + 23);
            return PCAP_OK;
        }
        case 'C': { /* Order Executed with Price (36 bytes) */
            if (len < 36) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_msg->type = PCAP_FEED_MSG_EXECUTE;
            out_msg->timestamp_ns = pcap_read_be48(data + 5);
            out_msg->order_id = pcap_read_be64(data + 11);
            out_msg->executed_qty = pcap_read_be32(data + 19);
            out_msg->match_id = pcap_read_be64(data + 23);
            out_msg->price = (uint64_t)pcap_read_be32(data + 32);
            return PCAP_OK;
        }
        case 'X': { /* Order Cancel (23 bytes) */
            if (len < 23) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_msg->type = PCAP_FEED_MSG_CANCEL;
            out_msg->timestamp_ns = pcap_read_be48(data + 5);
            out_msg->order_id = pcap_read_be64(data + 11);
            out_msg->qty = pcap_read_be32(data + 19);
            return PCAP_OK;
        }
        case 'D': { /* Order Delete (19 bytes) */
            if (len < 19) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_msg->type = PCAP_FEED_MSG_DELETE;
            out_msg->timestamp_ns = pcap_read_be48(data + 5);
            out_msg->order_id = pcap_read_be64(data + 11);
            return PCAP_OK;
        }
        case 'U': { /* Order Replace (35 bytes) */
            if (len < 35) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_msg->type = PCAP_FEED_MSG_REPLACE;
            out_msg->timestamp_ns = pcap_read_be48(data + 5);
            out_msg->order_id = pcap_read_be64(data + 11);
            out_msg->new_order_id = pcap_read_be64(data + 19);
            out_msg->qty = pcap_read_be32(data + 27);
            out_msg->price = (uint64_t)pcap_read_be32(data + 31);
            return PCAP_OK;
        }
        case 'P': { /* Non-Cross Trade (44 bytes) */
            if (len < 44) {
                return PCAP_ERR_TRUNCATED_PACKET;
            }
            out_msg->type = PCAP_FEED_MSG_TRADE;
            out_msg->timestamp_ns = pcap_read_be48(data + 5);
            out_msg->order_id = pcap_read_be64(data + 11);
            char side_ch = (char)data[19];
            out_msg->side = (side_ch == 'B') ? LOB_SIDE_BUY : LOB_SIDE_SELL;
            out_msg->qty = pcap_read_be32(data + 20);
            memcpy(out_msg->symbol, data + 24, 8);
            out_msg->symbol[8] = '\0';
            for (int i = 7; i >= 0 && out_msg->symbol[i] == ' '; i--) {
                out_msg->symbol[i] = '\0';
            }
            out_msg->price = (uint64_t)pcap_read_be32(data + 32);
            out_msg->match_id = pcap_read_be64(data + 36);
            return PCAP_OK;
        }
        default:
            return PCAP_ERR_PARSE_FAILED;
    }
}

pcap_result_t pcap_parse_moldudp64(const uint8_t *data, size_t len, pcap_feed_msg_callback_t on_msg,
                                   void *user_data) {
    if (!data || len < 20 || !on_msg) {
        return PCAP_ERR_INVALID_ARG;
    }

    uint16_t msg_count = pcap_read_le16(data + 18);
    size_t offset = 20;

    for (uint16_t i = 0; i < msg_count && offset < len; i++) {
        if (offset + 2 > len) {
            return PCAP_ERR_TRUNCATED_PACKET;
        }

        uint16_t msg_len = pcap_read_le16(data + offset);
        offset += 2;

        if (offset + msg_len > len) {
            return PCAP_ERR_TRUNCATED_PACKET;
        }

        pcap_feed_msg_t feed_msg;
        if (pcap_parse_itch_msg(data + offset, msg_len, &feed_msg) == PCAP_OK) {
            on_msg(&feed_msg, user_data);
        }

        offset += msg_len;
    }

    return PCAP_OK;
}

pcap_result_t pcap_parse_qfp(const uint8_t *data, size_t len, pcap_feed_msg_t *out_msg) {
    if (!data || !out_msg || len < sizeof(pcap_qfp_msg_t)) {
        return PCAP_ERR_INVALID_ARG;
    }

    const pcap_qfp_msg_t *qfp = (const pcap_qfp_msg_t *)data;
    memset(out_msg, 0, sizeof(pcap_feed_msg_t));

    out_msg->type = (pcap_feed_msg_type_t)qfp->msg_type;
    out_msg->side = qfp->side;
    out_msg->qty = qfp->qty;
    out_msg->price = qfp->price;
    out_msg->order_id = qfp->order_id;
    out_msg->timestamp_ns = qfp->timestamp_ns;

    return PCAP_OK;
}

lob_result_t pcap_feed_apply_to_book(lob_book_t *book, const pcap_feed_msg_t *msg) {
    if (!book || !msg) {
        return LOB_RESULT_INVALID_ARG;
    }

    switch (msg->type) {
        case PCAP_FEED_MSG_ADD:
            return lob_add_order(book, msg->order_id, (lob_side_t)msg->side, msg->price, msg->qty,
                                 LOB_TYPE_LIMIT, NULL, NULL);

        case PCAP_FEED_MSG_DELETE:
            return lob_cancel_order(book, msg->order_id);

        case PCAP_FEED_MSG_CANCEL: {
            const lob_order_t *ord = lob_find_order(book, msg->order_id);
            if (!ord) {
                return LOB_RESULT_ORDER_NOT_FOUND;
            }
            if (msg->qty >= ord->qty) {
                return lob_cancel_order(book, msg->order_id);
            }
            return lob_modify_order(book, msg->order_id, ord->qty - msg->qty, ord->price, NULL,
                                    NULL);
        }

        case PCAP_FEED_MSG_EXECUTE: {
            const lob_order_t *ord = lob_find_order(book, msg->order_id);
            if (!ord) {
                return LOB_RESULT_ORDER_NOT_FOUND;
            }
            uint32_t exec_qty = msg->executed_qty ? msg->executed_qty : msg->qty;
            if (exec_qty >= ord->qty) {
                return lob_cancel_order(book, msg->order_id);
            }
            return lob_modify_order(book, msg->order_id, ord->qty - exec_qty, ord->price, NULL,
                                    NULL);
        }

        case PCAP_FEED_MSG_REPLACE: {
            const lob_order_t *ord = lob_find_order(book, msg->order_id);
            uint8_t side = ord ? ord->side : msg->side;
            lob_cancel_order(book, msg->order_id);
            uint64_t target_id = msg->new_order_id ? msg->new_order_id : msg->order_id;
            return lob_add_order(book, target_id, (lob_side_t)side, msg->price, msg->qty,
                                 LOB_TYPE_LIMIT, NULL, NULL);
        }

        case PCAP_FEED_MSG_TRADE:
            return LOB_RESULT_OK; /* Non-book trade reported */

        default:
            return LOB_RESULT_INVALID_ARG;
    }
}

/* ----------------------------------------------------------------------------
 * Packet Construction & Encoders
 * ---------------------------------------------------------------------------- */

size_t pcap_encode_ethernet(uint8_t *buf, size_t max_len, const uint8_t dst_mac[6],
                            const uint8_t src_mac[6], uint16_t ethertype) {
    if (!buf || max_len < 14) {
        return 0;
    }
    memcpy(buf, dst_mac, 6);
    memcpy(buf + 6, src_mac, 6);
    pcap_write_be16(buf + 12, ethertype);
    return 14;
}

size_t pcap_encode_ipv4(uint8_t *buf, size_t max_len, uint32_t src_ip, uint32_t dst_ip,
                        uint8_t proto, uint16_t payload_len) {
    if (!buf || max_len < 20) {
        return 0;
    }
    uint16_t total_len = (uint16_t)(20 + payload_len);

    buf[0] = 0x45; /* Version 4, IHL 5 */
    buf[1] = 0x00; /* DSCP / ECN */
    pcap_write_be16(buf + 2, total_len);
    pcap_write_be16(buf + 4, 0x1234); /* Identification */
    pcap_write_be16(buf + 6, 0x4000); /* Don't Fragment */
    buf[8] = 64;                      /* TTL */
    buf[9] = proto;
    pcap_write_be16(buf + 10, 0x0000); /* Checksum placeholder */
    pcap_write_be32(buf + 12, src_ip);
    pcap_write_be32(buf + 16, dst_ip);

    /* Compute standard IPv4 header checksum */
    uint32_t sum = 0;
    for (size_t i = 0; i < 20; i += 2) {
        sum += (uint32_t)pcap_read_be16(buf + i);
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    pcap_write_be16(buf + 10, (uint16_t)~sum);

    return 20;
}

size_t pcap_encode_udp(uint8_t *buf, size_t max_len, uint16_t src_port, uint16_t dst_port,
                       uint16_t payload_len) {
    if (!buf || max_len < 8) {
        return 0;
    }
    uint16_t total_len = (uint16_t)(8 + payload_len);
    pcap_write_be16(buf, src_port);
    pcap_write_be16(buf + 2, dst_port);
    pcap_write_be16(buf + 4, total_len);
    pcap_write_be16(buf + 6, 0x0000); /* UDP checksum optional in IPv4 */
    return 8;
}

size_t pcap_encode_moldudp64(uint8_t *buf, size_t max_len, const char session[10], uint64_t seq_num,
                             uint16_t msg_count) {
    if (!buf || max_len < 20) {
        return 0;
    }
    if (session) {
        memcpy(buf, session, 10);
    } else {
        memset(buf, ' ', 10);
    }
    pcap_write_le64(buf + 10, seq_num);
    pcap_write_le16(buf + 18, msg_count);
    return 20;
}

size_t pcap_encode_itch_add(uint8_t *buf, size_t max_len, uint64_t timestamp_ns, uint64_t order_id,
                            uint8_t side, uint32_t shares, const char *symbol,
                            uint32_t price_fixed) {
    if (!buf || max_len < 36) {
        return 0;
    }
    buf[0] = 'A';
    pcap_write_be16(buf + 1, 1); /* Stock locate */
    pcap_write_be16(buf + 3, 0); /* Tracking number */
    pcap_write_be48(buf + 5, timestamp_ns);
    pcap_write_be64(buf + 11, order_id);
    buf[19] = (side == LOB_SIDE_BUY) ? 'B' : 'S';
    pcap_write_be32(buf + 20, shares);

    memset(buf + 24, ' ', 8);
    if (symbol) {
        size_t slen = strlen(symbol);
        if (slen > 8) {
            slen = 8;
        }
        memcpy(buf + 24, symbol, slen);
    }

    pcap_write_be32(buf + 32, price_fixed);
    return 36;
}

size_t pcap_encode_itch_execute(uint8_t *buf, size_t max_len, uint64_t timestamp_ns,
                                uint64_t order_id, uint32_t shares, uint64_t match_id) {
    if (!buf || max_len < 31) {
        return 0;
    }
    buf[0] = 'E';
    pcap_write_be16(buf + 1, 1);
    pcap_write_be16(buf + 3, 0);
    pcap_write_be48(buf + 5, timestamp_ns);
    pcap_write_be64(buf + 11, order_id);
    pcap_write_be32(buf + 19, shares);
    pcap_write_be64(buf + 23, match_id);
    return 31;
}

size_t pcap_encode_itch_cancel(uint8_t *buf, size_t max_len, uint64_t timestamp_ns,
                               uint64_t order_id, uint32_t shares) {
    if (!buf || max_len < 23) {
        return 0;
    }
    buf[0] = 'X';
    pcap_write_be16(buf + 1, 1);
    pcap_write_be16(buf + 3, 0);
    pcap_write_be48(buf + 5, timestamp_ns);
    pcap_write_be64(buf + 11, order_id);
    pcap_write_be32(buf + 19, shares);
    return 23;
}

size_t pcap_encode_itch_delete(uint8_t *buf, size_t max_len, uint64_t timestamp_ns,
                               uint64_t order_id) {
    if (!buf || max_len < 19) {
        return 0;
    }
    buf[0] = 'D';
    pcap_write_be16(buf + 1, 1);
    pcap_write_be16(buf + 3, 0);
    pcap_write_be48(buf + 5, timestamp_ns);
    pcap_write_be64(buf + 11, order_id);
    return 19;
}

size_t pcap_encode_itch_replace(uint8_t *buf, size_t max_len, uint64_t timestamp_ns,
                                uint64_t orig_order_id, uint64_t new_order_id, uint32_t shares,
                                uint32_t price_fixed) {
    if (!buf || max_len < 35) {
        return 0;
    }
    buf[0] = 'U';
    pcap_write_be16(buf + 1, 1);
    pcap_write_be16(buf + 3, 0);
    pcap_write_be48(buf + 5, timestamp_ns);
    pcap_write_be64(buf + 11, orig_order_id);
    pcap_write_be64(buf + 19, new_order_id);
    pcap_write_be32(buf + 27, shares);
    pcap_write_be32(buf + 31, price_fixed);
    return 35;
}

size_t pcap_encode_qfp(uint8_t *buf, size_t max_len, const pcap_qfp_msg_t *msg) {
    if (!buf || !msg || max_len < sizeof(pcap_qfp_msg_t)) {
        return 0;
    }
    memcpy(buf, msg, sizeof(pcap_qfp_msg_t));
    return sizeof(pcap_qfp_msg_t);
}

/* ----------------------------------------------------------------------------
 * Replay Engine Implementation
 * ---------------------------------------------------------------------------- */

struct pcap_replayer {
    pcap_reader_t *reader;
    pcap_replay_config_t config;
    pcap_replay_stats_t stats;
    bool has_first_packet;
    uint64_t sim_start_ns;
    uint64_t wall_start_ns;
    uint64_t prev_sim_ns;
};

pcap_replayer_t *pcap_replayer_create(pcap_reader_t *reader, const pcap_replay_config_t *config) {
    if (!reader) {
        return NULL;
    }

    pcap_replayer_t *replayer = (pcap_replayer_t *)calloc(1, sizeof(pcap_replayer_t));
    if (!replayer) {
        return NULL;
    }

    replayer->reader = reader;
    if (config) {
        replayer->config = *config;
    }
    if (replayer->config.end_time_ns == 0) {
        replayer->config.end_time_ns = UINT64_MAX;
    }

    return replayer;
}

void pcap_replayer_free(pcap_replayer_t *replayer) {
    free(replayer);
}

static bool pcap_replayer_matches_filter(pcap_replayer_t *replayer, const pcap_frame_t *frame,
                                         uint64_t timestamp_ns) {
    const pcap_replay_config_t *cfg = &replayer->config;

    if (timestamp_ns < cfg->start_time_ns || timestamp_ns > cfg->end_time_ns) {
        return false;
    }

    if (cfg->filter_proto && frame->has_ipv4 && frame->ip_proto != cfg->filter_proto) {
        return false;
    }

    if (cfg->filter_src_ip && frame->has_ipv4) {
        uint32_t mask = cfg->filter_ip_mask ? cfg->filter_ip_mask : 0xFFFFFFFFU;
        if ((frame->src_ip & mask) != (cfg->filter_src_ip & mask)) {
            return false;
        }
    }

    if (cfg->filter_dst_ip && frame->has_ipv4) {
        uint32_t mask = cfg->filter_ip_mask ? cfg->filter_ip_mask : 0xFFFFFFFFU;
        if ((frame->dst_ip & mask) != (cfg->filter_dst_ip & mask)) {
            return false;
        }
    }

    if (cfg->filter_src_port && frame->src_port != cfg->filter_src_port) {
        return false;
    }

    if (cfg->filter_dst_port && frame->dst_port != cfg->filter_dst_port) {
        return false;
    }

    return true;
}

static void pcap_replayer_pace(pcap_replayer_t *replayer, uint64_t timestamp_ns) {
    if (replayer->config.speed_multiplier <= 0.0) {
        return; /* AFAP mode: no pacing */
    }

    uint64_t now_wall = pcap_get_time_ns();

    if (!replayer->has_first_packet) {
        replayer->has_first_packet = true;
        replayer->sim_start_ns = timestamp_ns;
        replayer->wall_start_ns = now_wall;
        replayer->prev_sim_ns = timestamp_ns;
        return;
    }

    if (timestamp_ns < replayer->sim_start_ns) {
        return;
    }

    uint64_t sim_elapsed = timestamp_ns - replayer->sim_start_ns;
    uint64_t target_wall_elapsed =
        (uint64_t)((double)sim_elapsed / replayer->config.speed_multiplier);
    uint64_t target_wall_time = replayer->wall_start_ns + target_wall_elapsed;

    now_wall = pcap_get_time_ns();
    if (target_wall_time > now_wall) {
        uint64_t diff_ns = target_wall_time - now_wall;

        /* Sleep if delay is significant (> 100 microseconds) */
        if (diff_ns > 100000ULL) {
            struct timespec req;
            req.tv_sec = (time_t)((diff_ns - 50000ULL) / 1000000000ULL);
            req.tv_nsec = (long)((diff_ns - 50000ULL) % 1000000000ULL);
            nanosleep(&req, NULL);
        }

        /* High-resolution spin wait for sub-50us precision */
        while (pcap_get_time_ns() < target_wall_time) {
            /* busy wait */
        }
    }

    replayer->prev_sim_ns = timestamp_ns;
}

static void pcap_replayer_feed_cb(const pcap_feed_msg_t *msg, void *user_data) {
    pcap_replayer_t *replayer = (pcap_replayer_t *)user_data;
    replayer->stats.feed_messages++;
    if (replayer->config.on_feed_msg) {
        replayer->config.on_feed_msg(msg, replayer->config.user_data);
    }
}

pcap_result_t pcap_replayer_step(pcap_replayer_t *replayer, pcap_packet_t *out_pkt) {
    if (!replayer || !replayer->reader) {
        return PCAP_ERR_INVALID_ARG;
    }

    pcap_packet_t pkt;
    pcap_packet_t *target = out_pkt ? out_pkt : &pkt;

    while (1) {
        pcap_result_t res = pcap_reader_next_packet(replayer->reader, target);
        if (res != PCAP_OK) {
            return res;
        }

        replayer->stats.total_packets++;
        replayer->stats.total_bytes += target->incl_len;

        if (replayer->stats.start_timestamp_ns == 0) {
            replayer->stats.start_timestamp_ns = target->timestamp_ns;
        }
        replayer->stats.end_timestamp_ns = target->timestamp_ns;

        pcap_frame_t frame;
        const pcap_file_header_t *fhdr = pcap_reader_get_header(replayer->reader);
        uint32_t linktype = fhdr ? fhdr->network : PCAP_LINKTYPE_ETHERNET;

        pcap_decode_frame(target->data, target->incl_len, linktype, &frame);

        if (!pcap_replayer_matches_filter(replayer, &frame, target->timestamp_ns)) {
            continue;
        }

        replayer->stats.matched_packets++;

        /* Timing Pacing */
        pcap_replayer_pace(replayer, target->timestamp_ns);

        /* Packet Callback */
        if (replayer->config.on_packet) {
            replayer->config.on_packet(target, replayer->config.user_data);
        }

        /* Frame Callback */
        if (replayer->config.on_frame) {
            replayer->config.on_frame(&frame, target, replayer->config.user_data);
        }

        /* Feed Message Parsing & Dispatch */
        if (frame.payload && frame.payload_len > 0) {
            if (frame.payload_len >= 20 &&
                (frame.payload[0] == 'A' || frame.payload[0] == 'E' || frame.payload[0] == 'C' ||
                 frame.payload[0] == 'X' || frame.payload[0] == 'D' || frame.payload[0] == 'U' ||
                 frame.payload[0] == 'P')) {
                /* Raw ITCH message */
                pcap_feed_msg_t feed_msg;
                if (pcap_parse_itch_msg(frame.payload, frame.payload_len, &feed_msg) == PCAP_OK) {
                    pcap_replayer_feed_cb(&feed_msg, replayer);
                }
            } else if (frame.payload_len >= 20) {
                /* Attempt MoldUDP64 parsing */
                pcap_parse_moldudp64(frame.payload, frame.payload_len, pcap_replayer_feed_cb,
                                     replayer);
            } else if (frame.payload_len == sizeof(pcap_qfp_msg_t)) {
                /* QFP message */
                pcap_feed_msg_t feed_msg;
                if (pcap_parse_qfp(frame.payload, frame.payload_len, &feed_msg) == PCAP_OK) {
                    pcap_replayer_feed_cb(&feed_msg, replayer);
                }
            }
        }

        return PCAP_OK;
    }
}

pcap_result_t pcap_replayer_run(pcap_replayer_t *replayer) {
    if (!replayer) {
        return PCAP_ERR_INVALID_ARG;
    }

    uint64_t wall_start = pcap_get_time_ns();

    pcap_result_t res;
    while ((res = pcap_replayer_step(replayer, NULL)) == PCAP_OK) {
        /* step loops until EOF or error */
    }

    uint64_t wall_end = pcap_get_time_ns();
    replayer->stats.elapsed_wall_ns = wall_end - wall_start;

    if (replayer->stats.elapsed_wall_ns > 0) {
        double elapsed_sec = (double)replayer->stats.elapsed_wall_ns / 1e9;
        replayer->stats.packets_per_sec = (double)replayer->stats.total_packets / elapsed_sec;
        replayer->stats.mbps =
            ((double)replayer->stats.total_bytes * 8.0) / (elapsed_sec * 1000000.0);
    }

    return (res == PCAP_ERR_EOF) ? PCAP_OK : res;
}

void pcap_replayer_get_stats(const pcap_replayer_t *replayer, pcap_replay_stats_t *out_stats) {
    if (replayer && out_stats) {
        *out_stats = replayer->stats;
    }
}
