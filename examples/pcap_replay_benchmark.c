/* ============================================================================
 * pcap_replay_benchmark.c — Nanosecond PCAP Feed Replay & Ingestion Benchmark
 * ============================================================================ */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "order_book.h"
#include "pcap_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BENCH_PCAP_FILE "benchmark_feed.pcap"

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

typedef struct {
    lob_book_t *book;
    size_t orders_applied;
} bench_context_t;

static void benchmark_feed_handler(const pcap_feed_msg_t *msg, void *u) {
    bench_context_t *ctx = (bench_context_t *)u;
    pcap_feed_apply_to_book(ctx->book, msg);
    ctx->orders_applied++;
}

int main(int argc, char **argv) {
    size_t num_packets = 200000;
    if (argc > 1) {
        num_packets = (size_t)atoi(argv[1]);
        if (num_packets < 1000) {
            num_packets = 1000;
        }
    }

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   QForge — Nanosecond PCAP Replay & Feed Ingestion Benchmark ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    printf("  Generating synthetic exchange feed capture with %zu packets...\n", num_packets);

    /* Stage 1: Generate PCAP file with Ethernet + IPv4 + UDP + MoldUDP64 / ITCH */
    pcap_writer_t *writer = pcap_writer_create(BENCH_PCAP_FILE, true, PCAP_LINKTYPE_ETHERNET);
    if (!writer) {
        fprintf(stderr, "Failed to create benchmark PCAP file\n");
        return 1;
    }

    uint8_t dst_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t src_mac[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    uint32_t src_ip = 0xC0A80164; /* 192.168.1.100 */
    uint32_t dst_ip = 0xE0000101; /* 224.0.1.1 */

    double gen_t0 = get_time_sec();
    uint8_t pkt_buf[256];

    for (size_t i = 1; i <= num_packets; i++) {
        uint64_t ts_ns = 1700000000000000000ULL + (uint64_t)i * 100ULL;
        size_t elen =
            pcap_encode_ethernet(pkt_buf, sizeof(pkt_buf), dst_mac, src_mac, PCAP_ETHERTYPE_IPV4);
        size_t ilen = pcap_encode_ipv4(pkt_buf + elen, sizeof(pkt_buf) - elen, src_ip, dst_ip,
                                       PCAP_PROTO_UDP, 8 + 36);
        size_t ulen =
            pcap_encode_udp(pkt_buf + elen + ilen, sizeof(pkt_buf) - elen - ilen, 5000, 5001, 36);

        uint8_t side = (i % 2 == 0) ? LOB_SIDE_BUY : LOB_SIDE_SELL;
        uint64_t price = 15000 + (i % 500);
        if (side == LOB_SIDE_SELL) {
            price += 1000; /* No crossing */
        }

        pcap_encode_itch_add(pkt_buf + elen + ilen + ulen, sizeof(pkt_buf) - elen - ilen - ulen,
                             ts_ns, i, side, 100, "AAPL", (uint32_t)price);

        pcap_writer_write_packet(writer, ts_ns, pkt_buf, (uint32_t)(elen + ilen + ulen + 36));
    }
    pcap_writer_close(writer);
    double gen_t1 = get_time_sec();

    printf("  Generated %zu packets in %.3f sec\n\n", num_packets, gen_t1 - gen_t0);

    /* Stage 2: Pure Binary Packet Parsing Throughput (AFAP Line Rate) */
    pcap_reader_t *reader = pcap_reader_open(BENCH_PCAP_FILE);
    if (!reader) {
        fprintf(stderr, "Failed to open benchmark PCAP file\n");
        unlink(BENCH_PCAP_FILE);
        return 1;
    }

    pcap_replay_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.speed_multiplier = 0.0; /* Line rate / AFAP */

    pcap_replayer_t *replayer = pcap_replayer_create(reader, &cfg);

    double t0 = get_time_sec();
    pcap_replayer_run(replayer);
    double t1 = get_time_sec();

    pcap_replay_stats_t stats;
    pcap_replayer_get_stats(replayer, &stats);

    double parse_time = t1 - t0;
    double pps = (double)stats.total_packets / parse_time;
    double mbps = ((double)stats.total_bytes * 8.0) / (parse_time * 1e6);
    double ns_per_pkt = (parse_time / (double)stats.total_packets) * 1e9;

    printf("  [Stage 1: Pure Binary Parsing & Frame Decoding]\n");
    printf("    Packets Parsed: %llu\n", (unsigned long long)stats.total_packets);
    printf("    Bytes Parsed:   %llu bytes (%.2f MB)\n", (unsigned long long)stats.total_bytes,
           (double)stats.total_bytes / (1024.0 * 1024.0));
    printf("    Feed Messages:  %llu\n", (unsigned long long)stats.feed_messages);
    printf("    Elapsed Time:   %.4f sec\n", parse_time);
    printf("    Throughput:     %.2f million packets/sec (%.2f Mbps)\n", pps / 1e6, mbps);
    printf("    Latency:        %.1f ns / packet\n\n", ns_per_pkt);

    pcap_replayer_free(replayer);
    pcap_reader_close(reader);

    /* Stage 3: End-to-End Feed Replay + L3 Order Book Ingestion */
    reader = pcap_reader_open(BENCH_PCAP_FILE);
    lob_book_t *book = lob_create(num_packets + 1024, 4096);

    bench_context_t bctx = {book, 0};

    memset(&cfg, 0, sizeof(cfg));
    cfg.speed_multiplier = 0.0;
    cfg.on_feed_msg = benchmark_feed_handler;
    cfg.user_data = &bctx;

    replayer = pcap_replayer_create(reader, &cfg);

    t0 = get_time_sec();
    pcap_replayer_run(replayer);
    t1 = get_time_sec();

    double e2e_time = t1 - t0;
    double e2e_rate = (double)bctx.orders_applied / e2e_time;
    double e2e_latency_ns = (e2e_time / (double)bctx.orders_applied) * 1e9;

    printf("  [Stage 2: End-to-End Replay + L3 Order Book Ingestion]\n");
    printf("    Orders Processed: %zu\n", bctx.orders_applied);
    printf("    Book Resting Count: %zu\n", lob_order_count(book));
    printf("    Elapsed Time:     %.4f sec\n", e2e_time);
    printf("    Throughput:       %.2f million orders/sec\n", e2e_rate / 1e6);
    printf("    Latency:          %.1f ns / order (Parser + Network Stack + Book)\n\n",
           e2e_latency_ns);

    pcap_replayer_free(replayer);
    pcap_reader_close(reader);
    lob_free(book);

    unlink(BENCH_PCAP_FILE);
    printf("  Benchmark completed successfully!\n\n");
    return 0;
}
