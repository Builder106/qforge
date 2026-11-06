/* ============================================================================
 * test_pcap_parser.c — Unit Tests for PCAP Binary Parser & Feed Replayer
 * ============================================================================ */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "order_book.h"
#include "pcap_parser.h"
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_PCAP_FILE "test_temp_capture.pcap"

/* ----------------------------------------------------------------------------
 * Test 1: Global Header Parsing & Magic Numbers
 * ---------------------------------------------------------------------------- */

void test_pcap_global_header_parsing(void) {
    /* 1. Native nanosecond header */
    uint8_t nano_buf[24];
    uint32_t magic_nano = PCAP_MAGIC_NANOBSEC_NATIVE;
    uint16_t v_maj = 2, v_min = 4;
    int32_t tz = 0;
    uint32_t sigfigs = 0, snaplen = 65535, net = PCAP_LINKTYPE_ETHERNET;

    memcpy(nano_buf + 0, &magic_nano, 4);
    memcpy(nano_buf + 4, &v_maj, 2);
    memcpy(nano_buf + 6, &v_min, 2);
    memcpy(nano_buf + 8, &tz, 4);
    memcpy(nano_buf + 12, &sigfigs, 4);
    memcpy(nano_buf + 16, &snaplen, 4);
    memcpy(nano_buf + 20, &net, 4);

    pcap_reader_t *r_nano = pcap_reader_open_memory(nano_buf, sizeof(nano_buf));
    ASSERT_TRUE(r_nano != NULL);
    ASSERT_TRUE(pcap_reader_is_nanosecond(r_nano));
    ASSERT_TRUE(!pcap_reader_is_swapped(r_nano));
    const pcap_file_header_t *hdr = pcap_reader_get_header(r_nano);
    ASSERT_TRUE(hdr != NULL);
    ASSERT_EQ(hdr->snaplen, 65535);
    ASSERT_EQ(hdr->network, PCAP_LINKTYPE_ETHERNET);
    pcap_reader_close(r_nano);

    /* 2. Microsecond native header */
    uint8_t micro_buf[24];
    uint32_t magic_micro = PCAP_MAGIC_MICROSEC_NATIVE;
    memcpy(micro_buf, nano_buf, 24);
    memcpy(micro_buf, &magic_micro, 4);

    pcap_reader_t *r_micro = pcap_reader_open_memory(micro_buf, sizeof(micro_buf));
    ASSERT_TRUE(r_micro != NULL);
    ASSERT_TRUE(!pcap_reader_is_nanosecond(r_micro));
    ASSERT_TRUE(!pcap_reader_is_swapped(r_micro));
    pcap_reader_close(r_micro);

    /* 3. Swapped nanosecond header */
    uint8_t swapped_buf[24];
    uint32_t magic_swap = PCAP_MAGIC_NANOBSEC_SWAPPED;
    memcpy(swapped_buf, nano_buf, 24);
    memcpy(swapped_buf, &magic_swap, 4);
    /* swap version major (2 -> 0x0200) */
    uint16_t v_maj_swap = 0x0002;
    uint8_t *p_v = (uint8_t *)&v_maj_swap;
    uint16_t v_swapped = ((uint16_t)p_v[0] << 8) | p_v[1];
    memcpy(swapped_buf + 4, &v_swapped, 2);

    pcap_reader_t *r_swap = pcap_reader_open_memory(swapped_buf, sizeof(swapped_buf));
    ASSERT_TRUE(r_swap != NULL);
    ASSERT_TRUE(pcap_reader_is_nanosecond(r_swap));
    ASSERT_TRUE(pcap_reader_is_swapped(r_swap));
    ASSERT_EQ(pcap_reader_get_header(r_swap)->version_major, 2);
    pcap_reader_close(r_swap);

    /* 4. Bad magic */
    uint8_t bad_buf[24];
    memset(bad_buf, 0xFF, 24);
    pcap_reader_t *r_bad = pcap_reader_open_memory(bad_buf, sizeof(bad_buf));
    ASSERT_TRUE(r_bad == NULL);
}

/* ----------------------------------------------------------------------------
 * Test 2: PCAP Writer and File Reading Roundtrip
 * ---------------------------------------------------------------------------- */

void test_pcap_writer_and_reader_roundtrip(void) {
    pcap_writer_t *writer = pcap_writer_create(TEST_PCAP_FILE, true, PCAP_LINKTYPE_ETHERNET);
    ASSERT_TRUE(writer != NULL);

    /* Write 3 sample packets with nanosecond timestamps */
    uint8_t pkt1[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t pkt2[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    uint8_t pkt3[] = {0xaa, 0xbb, 0xcc, 0xdd};

    uint64_t t1 = 1700000000123456789ULL;
    uint64_t t2 = 1700000000234567890ULL;
    uint64_t t3 = 1700000000345678901ULL;

    ASSERT_EQ(pcap_writer_write_packet(writer, t1, pkt1, sizeof(pkt1)), PCAP_OK);
    ASSERT_EQ(pcap_writer_write_packet(writer, t2, pkt2, sizeof(pkt2)), PCAP_OK);
    ASSERT_EQ(pcap_writer_write_packet(writer, t3, pkt3, sizeof(pkt3)), PCAP_OK);
    pcap_writer_close(writer);

    /* Read back using file reader */
    pcap_reader_t *reader = pcap_reader_open(TEST_PCAP_FILE);
    ASSERT_TRUE(reader != NULL);
    ASSERT_TRUE(pcap_reader_is_nanosecond(reader));

    pcap_packet_t p;
    ASSERT_EQ(pcap_reader_next_packet(reader, &p), PCAP_OK);
    ASSERT_EQ(p.timestamp_ns, t1);
    ASSERT_EQ(p.incl_len, sizeof(pkt1));
    ASSERT_EQ(memcmp(p.data, pkt1, sizeof(pkt1)), 0);

    ASSERT_EQ(pcap_reader_next_packet(reader, &p), PCAP_OK);
    ASSERT_EQ(p.timestamp_ns, t2);
    ASSERT_EQ(p.incl_len, sizeof(pkt2));
    ASSERT_EQ(memcmp(p.data, pkt2, sizeof(pkt2)), 0);

    ASSERT_EQ(pcap_reader_next_packet(reader, &p), PCAP_OK);
    ASSERT_EQ(p.timestamp_ns, t3);
    ASSERT_EQ(p.incl_len, sizeof(pkt3));
    ASSERT_EQ(memcmp(p.data, pkt3, sizeof(pkt3)), 0);

    /* EOF check */
    ASSERT_EQ(pcap_reader_next_packet(reader, &p), PCAP_ERR_EOF);

    /* Rewind and verify first packet again */
    ASSERT_EQ(pcap_reader_rewind(reader), PCAP_OK);
    ASSERT_EQ(pcap_reader_next_packet(reader, &p), PCAP_OK);
    ASSERT_EQ(p.timestamp_ns, t1);

    pcap_reader_close(reader);
    unlink(TEST_PCAP_FILE);
}

/* ----------------------------------------------------------------------------
 * Test 3: Network Protocol Stack Decoding (Ethernet, VLAN, IPv4, UDP, TCP)
 * ---------------------------------------------------------------------------- */

void test_pcap_protocol_decoding(void) {
    uint8_t buffer[256];
    uint8_t dst_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t src_mac[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};

    /* 1. Ethernet + IPv4 + UDP packet */
    size_t eth_len =
        pcap_encode_ethernet(buffer, sizeof(buffer), dst_mac, src_mac, PCAP_ETHERTYPE_IPV4);
    ASSERT_EQ(eth_len, 14);

    uint32_t src_ip = 0xC0A80164; /* 192.168.1.100 */
    uint32_t dst_ip = 0xE0000101; /* 224.0.1.1 (Multicast) */
    uint16_t src_port = 12345;
    uint16_t dst_port = 5001;
    const char *payload_data = "HELLO_EXCHANGE";
    size_t payload_len = strlen(payload_data);

    size_t ip_len = pcap_encode_ipv4(buffer + eth_len, sizeof(buffer) - eth_len, src_ip, dst_ip,
                                     PCAP_PROTO_UDP, 8 + payload_len);
    ASSERT_EQ(ip_len, 20);

    size_t udp_len = pcap_encode_udp(buffer + eth_len + ip_len, sizeof(buffer) - eth_len - ip_len,
                                     src_port, dst_port, payload_len);
    ASSERT_EQ(udp_len, 8);

    memcpy(buffer + eth_len + ip_len + udp_len, payload_data, payload_len);
    size_t total_len = eth_len + ip_len + udp_len + payload_len;

    pcap_frame_t frame;
    ASSERT_EQ(pcap_decode_frame(buffer, (uint32_t)total_len, PCAP_LINKTYPE_ETHERNET, &frame),
              PCAP_OK);
    ASSERT_TRUE(!frame.has_vlan);
    ASSERT_TRUE(frame.has_ipv4);
    ASSERT_EQ(frame.ethertype, PCAP_ETHERTYPE_IPV4);
    ASSERT_EQ(frame.src_ip, src_ip);
    ASSERT_EQ(frame.dst_ip, dst_ip);
    ASSERT_EQ(frame.ip_proto, PCAP_PROTO_UDP);
    ASSERT_EQ(frame.src_port, src_port);
    ASSERT_EQ(frame.dst_port, dst_port);
    ASSERT_EQ(frame.payload_len, payload_len);
    ASSERT_EQ(memcmp(frame.payload, payload_data, payload_len), 0);

    /* 2. String conversion utilities */
    char ip_str[32];
    pcap_ip_to_str(src_ip, ip_str, sizeof(ip_str));
    ASSERT_TRUE(strcmp(ip_str, "192.168.1.100") == 0);

    uint32_t parsed_ip = 0;
    ASSERT_TRUE(pcap_str_to_ip("192.168.1.100", &parsed_ip));
    ASSERT_EQ(parsed_ip, src_ip);

    char mac_str[32];
    pcap_mac_to_str(src_mac, mac_str, sizeof(mac_str));
    ASSERT_TRUE(strcmp(mac_str, "66:77:88:99:aa:bb") == 0);

    /* 3. IEEE 802.1Q VLAN Tagged Frame */
    uint8_t vlan_buf[256];
    memcpy(vlan_buf, dst_mac, 6);
    memcpy(vlan_buf + 6, src_mac, 6);
    vlan_buf[12] = 0x81;
    vlan_buf[13] = 0x00; /* EtherType VLAN */
    vlan_buf[14] = 0x00;
    vlan_buf[15] = 0x64; /* VLAN ID = 100 */
    vlan_buf[16] = 0x08;
    vlan_buf[17] = 0x00; /* Encapsulated IPv4 */
    /* Copy IP + UDP + payload */
    memcpy(vlan_buf + 18, buffer + eth_len, ip_len + udp_len + payload_len);
    size_t vlan_total = 18 + ip_len + udp_len + payload_len;

    pcap_frame_t vlan_frame;
    ASSERT_EQ(
        pcap_decode_frame(vlan_buf, (uint32_t)vlan_total, PCAP_LINKTYPE_ETHERNET, &vlan_frame),
        PCAP_OK);
    ASSERT_TRUE(vlan_frame.has_vlan);
    ASSERT_EQ(vlan_frame.vlan_id, 100);
    ASSERT_EQ(vlan_frame.ethertype, PCAP_ETHERTYPE_IPV4);
    ASSERT_EQ(vlan_frame.dst_port, dst_port);
}

/* ----------------------------------------------------------------------------
 * Test 4: NASDAQ ITCH 5.0 Message Encoding and Parsing
 * ---------------------------------------------------------------------------- */

void test_pcap_itch_parsing(void) {
    uint8_t buf[128];
    pcap_feed_msg_t msg;

    /* Message 'A' (Add Order) */
    size_t len_a = pcap_encode_itch_add(buf, sizeof(buf), 36000000000000ULL, 1001, LOB_SIDE_BUY,
                                        500, "AAPL", 1502500);
    ASSERT_EQ(len_a, 36);
    ASSERT_EQ(pcap_parse_itch_msg(buf, len_a, &msg), PCAP_OK);
    ASSERT_EQ(msg.type, PCAP_FEED_MSG_ADD);
    ASSERT_EQ(msg.timestamp_ns, 36000000000000ULL);
    ASSERT_EQ(msg.order_id, 1001);
    ASSERT_EQ(msg.side, LOB_SIDE_BUY);
    ASSERT_EQ(msg.qty, 500);
    ASSERT_TRUE(strcmp(msg.symbol, "AAPL") == 0);
    ASSERT_EQ(msg.price, 1502500);

    /* Message 'E' (Executed) */
    size_t len_e = pcap_encode_itch_execute(buf, sizeof(buf), 36001000000000ULL, 1001, 200, 9999);
    ASSERT_EQ(len_e, 31);
    ASSERT_EQ(pcap_parse_itch_msg(buf, len_e, &msg), PCAP_OK);
    ASSERT_EQ(msg.type, PCAP_FEED_MSG_EXECUTE);
    ASSERT_EQ(msg.order_id, 1001);
    ASSERT_EQ(msg.executed_qty, 200);
    ASSERT_EQ(msg.match_id, 9999);

    /* Message 'X' (Cancel) */
    size_t len_x = pcap_encode_itch_cancel(buf, sizeof(buf), 36002000000000ULL, 1001, 100);
    ASSERT_EQ(len_x, 23);
    ASSERT_EQ(pcap_parse_itch_msg(buf, len_x, &msg), PCAP_OK);
    ASSERT_EQ(msg.type, PCAP_FEED_MSG_CANCEL);
    ASSERT_EQ(msg.order_id, 1001);
    ASSERT_EQ(msg.qty, 100);

    /* Message 'D' (Delete) */
    size_t len_d = pcap_encode_itch_delete(buf, sizeof(buf), 36003000000000ULL, 1001);
    ASSERT_EQ(len_d, 19);
    ASSERT_EQ(pcap_parse_itch_msg(buf, len_d, &msg), PCAP_OK);
    ASSERT_EQ(msg.type, PCAP_FEED_MSG_DELETE);
    ASSERT_EQ(msg.order_id, 1001);

    /* Message 'U' (Replace) */
    size_t len_u =
        pcap_encode_itch_replace(buf, sizeof(buf), 36004000000000ULL, 1001, 1002, 300, 1503000);
    ASSERT_EQ(len_u, 35);
    ASSERT_EQ(pcap_parse_itch_msg(buf, len_u, &msg), PCAP_OK);
    ASSERT_EQ(msg.type, PCAP_FEED_MSG_REPLACE);
    ASSERT_EQ(msg.order_id, 1001);
    ASSERT_EQ(msg.new_order_id, 1002);
    ASSERT_EQ(msg.qty, 300);
    ASSERT_EQ(msg.price, 1503000);
}

/* ----------------------------------------------------------------------------
 * Test 5: MoldUDP64 Multicast Packet Parsing
 * ---------------------------------------------------------------------------- */

typedef struct {
    int count;
    uint64_t last_order_id;
} mold_test_context_t;

static void mold_callback(const pcap_feed_msg_t *msg, void *user_data) {
    mold_test_context_t *ctx = (mold_test_context_t *)user_data;
    ctx->count++;
    ctx->last_order_id = msg->order_id;
}

void test_pcap_moldudp64_parsing(void) {
    uint8_t buffer[512];
    size_t offset = pcap_encode_moldudp64(buffer, sizeof(buffer), "SESSION001", 10001, 3);
    ASSERT_EQ(offset, 20);

    /* Pack message 1: Add Order 1 */
    uint8_t m1[36];
    pcap_encode_itch_add(m1, sizeof(m1), 1000, 101, LOB_SIDE_BUY, 100, "MSFT", 30000);
    buffer[offset] = 36;
    buffer[offset + 1] = 0; /* msg_len = 36 (little endian) */
    memcpy(buffer + offset + 2, m1, 36);
    offset += 2 + 36;

    /* Pack message 2: Add Order 2 */
    uint8_t m2[36];
    pcap_encode_itch_add(m2, sizeof(m2), 2000, 102, LOB_SIDE_SELL, 200, "MSFT", 30100);
    buffer[offset] = 36;
    buffer[offset + 1] = 0;
    memcpy(buffer + offset + 2, m2, 36);
    offset += 2 + 36;

    /* Pack message 3: Order Executed */
    uint8_t m3[31];
    pcap_encode_itch_execute(m3, sizeof(m3), 3000, 101, 50, 888);
    buffer[offset] = 31;
    buffer[offset + 1] = 0;
    memcpy(buffer + offset + 2, m3, 31);
    offset += 2 + 31;

    mold_test_context_t ctx = {0, 0};
    ASSERT_EQ(pcap_parse_moldudp64(buffer, offset, mold_callback, &ctx), PCAP_OK);
    ASSERT_EQ(ctx.count, 3);
    ASSERT_EQ(ctx.last_order_id, 101);
}

/* ----------------------------------------------------------------------------
 * Test 6: QFP Native Format & Feed to Order Book Execution
 * ---------------------------------------------------------------------------- */

void test_pcap_feed_to_order_book(void) {
    lob_book_t *book = lob_create(1000, 100);
    ASSERT_TRUE(book != NULL);

    /* 1. Add resting Buy limit order @ 100 ticks, qty 50 */
    pcap_feed_msg_t m_buy;
    memset(&m_buy, 0, sizeof(m_buy));
    m_buy.type = PCAP_FEED_MSG_ADD;
    m_buy.order_id = 1;
    m_buy.side = LOB_SIDE_BUY;
    m_buy.price = 100;
    m_buy.qty = 50;
    ASSERT_EQ(pcap_feed_apply_to_book(book, &m_buy), LOB_RESULT_OK);

    /* 2. Add resting Sell limit order @ 105 ticks, qty 100 */
    pcap_feed_msg_t m_sell;
    memset(&m_sell, 0, sizeof(m_sell));
    m_sell.type = PCAP_FEED_MSG_ADD;
    m_sell.order_id = 2;
    m_sell.side = LOB_SIDE_SELL;
    m_sell.price = 105;
    m_sell.qty = 100;
    ASSERT_EQ(pcap_feed_apply_to_book(book, &m_sell), LOB_RESULT_OK);

    uint64_t b_price = 0, a_price = 0;
    uint32_t b_qty = 0, a_qty = 0;
    ASSERT_TRUE(lob_get_bbo(book, &b_price, &b_qty, &a_price, &a_qty));
    ASSERT_EQ(b_price, 100);
    ASSERT_EQ(b_qty, 50);
    ASSERT_EQ(a_price, 105);
    ASSERT_EQ(a_qty, 100);

    /* 3. Send aggressive crossing Buy order @ 105 ticks, qty 40 */
    pcap_feed_msg_t m_cross;
    memset(&m_cross, 0, sizeof(m_cross));
    m_cross.type = PCAP_FEED_MSG_ADD;
    m_cross.order_id = 3;
    m_cross.side = LOB_SIDE_BUY;
    m_cross.price = 105;
    m_cross.qty = 40;
    ASSERT_EQ(pcap_feed_apply_to_book(book, &m_cross), LOB_RESULT_OK);

    /* Order 2 should now have 100 - 40 = 60 shares remaining at ask 105 */
    const lob_order_t *o2 = lob_find_order(book, 2);
    ASSERT_TRUE(o2 != NULL);
    ASSERT_EQ(o2->qty, 60);

    /* 4. Cancel order 1 */
    pcap_feed_msg_t m_del;
    memset(&m_del, 0, sizeof(m_del));
    m_del.type = PCAP_FEED_MSG_DELETE;
    m_del.order_id = 1;
    ASSERT_EQ(pcap_feed_apply_to_book(book, &m_del), LOB_RESULT_OK);
    ASSERT_TRUE(lob_find_order(book, 1) == NULL);
    b_price = 0;
    b_qty = 0;
    lob_get_bbo(book, &b_price, &b_qty, &a_price, &a_qty);
    ASSERT_EQ(b_price, 0);
    ASSERT_EQ(b_qty, 0);

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * Test 7: Nanosecond Replayer in AFAP & Paced Mode
 * ---------------------------------------------------------------------------- */

void test_pcap_replayer_modes(void) {
    pcap_writer_t *writer = pcap_writer_create(TEST_PCAP_FILE, true, PCAP_LINKTYPE_ETHERNET);
    ASSERT_TRUE(writer != NULL);

    uint8_t dst_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t src_mac[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    uint32_t ip1 = 0x0A000001; /* 10.0.0.1 */
    uint32_t ip2 = 0x0A000002; /* 10.0.0.2 */

    /* Write 10 synthetic packets */
    for (int i = 0; i < 10; i++) {
        uint8_t packet[128];
        size_t elen =
            pcap_encode_ethernet(packet, sizeof(packet), dst_mac, src_mac, PCAP_ETHERTYPE_IPV4);
        size_t ilen = pcap_encode_ipv4(packet + elen, sizeof(packet) - elen, ip1, ip2,
                                       PCAP_PROTO_UDP, 8 + 36);
        size_t ulen = pcap_encode_udp(packet + elen + ilen, sizeof(packet) - elen - ilen, 5000,
                                      5001 + (i % 2), 36);
        pcap_encode_itch_add(packet + elen + ilen + ulen, sizeof(packet) - elen - ilen - ulen,
                             1000000000ULL + (uint64_t)i * 1000000ULL, 500 + i, LOB_SIDE_BUY, 100,
                             "TEST", 10000);
        size_t total = elen + ilen + ulen + 36;
        pcap_writer_write_packet(writer, 1000000000ULL + (uint64_t)i * 1000000ULL, packet,
                                 (uint32_t)total);
    }
    pcap_writer_close(writer);

    /* AFAP Replay */
    pcap_reader_t *reader = pcap_reader_open(TEST_PCAP_FILE);
    ASSERT_TRUE(reader != NULL);

    pcap_replay_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.speed_multiplier = 0.0; /* Line rate */

    pcap_replayer_t *rep = pcap_replayer_create(reader, &cfg);
    ASSERT_TRUE(rep != NULL);
    ASSERT_EQ(pcap_replayer_run(rep), PCAP_OK);

    pcap_replay_stats_t stats;
    pcap_replayer_get_stats(rep, &stats);
    ASSERT_EQ(stats.total_packets, 10);
    ASSERT_EQ(stats.matched_packets, 10);
    ASSERT_EQ(stats.feed_messages, 10);
    ASSERT_TRUE(stats.packets_per_sec > 0.0);

    pcap_replayer_free(rep);
    pcap_reader_close(reader);

    /* Filtered Replay (filter destination port 5001 only -> 5 packets match) */
    reader = pcap_reader_open(TEST_PCAP_FILE);
    ASSERT_TRUE(reader != NULL);

    memset(&cfg, 0, sizeof(cfg));
    cfg.filter_dst_port = 5001;

    rep = pcap_replayer_create(reader, &cfg);
    ASSERT_EQ(pcap_replayer_run(rep), PCAP_OK);

    pcap_replayer_get_stats(rep, &stats);
    ASSERT_EQ(stats.total_packets, 10);
    ASSERT_EQ(stats.matched_packets, 5);
    ASSERT_EQ(stats.feed_messages, 5);

    pcap_replayer_free(rep);
    pcap_reader_close(reader);

    unlink(TEST_PCAP_FILE);
}

/* ----------------------------------------------------------------------------
 * Test 8: Error Handling and Boundary Conditions
 * ---------------------------------------------------------------------------- */

void test_pcap_error_handling(void) {
    pcap_packet_t pkt;
    ASSERT_EQ(pcap_reader_next_packet(NULL, &pkt), PCAP_ERR_INVALID_ARG);
    ASSERT_EQ(pcap_reader_rewind(NULL), PCAP_ERR_INVALID_ARG);
    ASSERT_TRUE(pcap_reader_open("non_existent_file_path.pcap") == NULL);
    ASSERT_TRUE(pcap_reader_open_memory(NULL, 0) == NULL);

    /* Corrupt packet data */
    uint8_t short_pkt[10] = {0};
    pcap_frame_t frame;
    ASSERT_EQ(pcap_decode_frame(short_pkt, 10, PCAP_LINKTYPE_ETHERNET, &frame),
              PCAP_ERR_TRUNCATED_PACKET);

    pcap_feed_msg_t fmsg;
    ASSERT_EQ(pcap_parse_itch_msg(short_pkt, 10, &fmsg), PCAP_ERR_PARSE_FAILED);
    short_pkt[0] = 'A';
    ASSERT_EQ(pcap_parse_itch_msg(short_pkt, 10, &fmsg), PCAP_ERR_TRUNCATED_PACKET);
}
