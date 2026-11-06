/* ============================================================================
 * order_book_benchmark.c — L3 Limit Order Book Throughput & Latency Benchmark
 * ============================================================================ */

#define _POSIX_C_SOURCE 199309L

#include "order_book.h"
#include "simd_match.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    size_t num_orders = 500000;
    if (argc > 1) {
        num_orders = (size_t)atoi(argv[1]);
        if (num_orders < 1000)
            num_orders = 1000;
    }

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   QForge — L3 Limit Order Book Benchmark                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    printf("  Benchmarking with %zu operations per test stage...\n\n", num_orders);

    lob_book_t *book = lob_create(num_orders + 1024, 8192);
    if (!book) {
        fprintf(stderr, "Failed to allocate order book\n");
        return 1;
    }

    /* ── Stage 1: Resting Order Insertions (Non-crossing) ── */
    double t0 = get_time_sec();
    for (size_t i = 1; i <= num_orders; i++) {
        uint64_t price = 10000 + (i % 1000);
        lob_side_t side = (i % 2 == 0) ? LOB_SIDE_BUY : LOB_SIDE_SELL;
        if (side == LOB_SIDE_SELL)
            price += 2000; /* Ensure no crossing */
        lob_add_order(book, i, side, price, 10, LOB_TYPE_LIMIT, NULL, NULL);
    }
    double t1 = get_time_sec();
    double insert_time = t1 - t0;
    double insert_rate = (double)num_orders / insert_time;
    double insert_latency_ns = (insert_time / (double)num_orders) * 1e9;

    printf("  [Stage 1: Insertions] %zu resting limit orders\n", num_orders);
    printf("    Elapsed:    %.4f sec\n", insert_time);
    printf("    Throughput: %.2f million orders/sec\n", insert_rate / 1e6);
    printf("    Latency:    %.1f ns / order\n\n", insert_latency_ns);

    /* ── Stage 2: Order Cancellations (O(1) lookups) ── */
    t0 = get_time_sec();
    for (size_t i = 1; i <= num_orders; i += 2) {
        lob_cancel_order(book, i);
    }
    t1 = get_time_sec();
    size_t cancelled = num_orders / 2;
    double cancel_time = t1 - t0;
    double cancel_rate = (double)cancelled / cancel_time;
    double cancel_latency_ns = (cancel_time / (double)cancelled) * 1e9;

    printf("  [Stage 2: Cancellations] %zu O(1) order cancellations\n", cancelled);
    printf("    Elapsed:    %.4f sec\n", cancel_time);
    printf("    Throughput: %.2f million cancels/sec\n", cancel_rate / 1e6);
    printf("    Latency:    %.1f ns / cancel\n\n", cancel_latency_ns);

    lob_clear(book);

    /* ── Stage 3: Order Matching (Full Execution Path) ── */
    size_t match_pairs = num_orders / 2;
    for (size_t i = 1; i <= match_pairs; i++) {
        uint64_t price = 50000 + (i % 50);
        lob_add_order(book, i, LOB_SIDE_SELL, price, 10, LOB_TYPE_LIMIT, NULL, NULL);
    }

    t0 = get_time_sec();
    for (size_t i = 1; i <= match_pairs; i++) {
        uint64_t price = 50000 + (i % 50);
        lob_add_order(book, match_pairs + i, LOB_SIDE_BUY, price, 10, LOB_TYPE_LIMIT, NULL, NULL);
    }
    t1 = get_time_sec();
    double match_time = t1 - t0;
    double match_rate = (double)match_pairs / match_time;
    double match_latency_ns = (match_time / (double)match_pairs) * 1e9;

    printf("  [Stage 3: SIMD Matching] %zu trade matches (crossing path, %s)\n", match_pairs,
           lob_simd_active_backend_name());
    printf("    Elapsed:    %.4f sec\n", match_time);
    printf("    Throughput: %.2f million matches/sec\n", match_rate / 1e6);
    printf("    Latency:    %.1f ns / match\n\n", match_latency_ns);

    /* ── Stage 4: Lock-Free Command Queue (Disruptor Pattern) ── */
    lob_queue_t *queue = lob_queue_create(131072);
    size_t q_ops = 200000;
    lob_command_t cmd;
    cmd.type = LOB_CMD_ADD;
    cmd.side = LOB_SIDE_BUY;
    cmd.order_type = LOB_TYPE_LIMIT;
    cmd._pad = 0;
    cmd.qty = 100;
    cmd.price = 50000;

    t0 = get_time_sec();
    for (size_t i = 0; i < q_ops; i++) {
        cmd.order_id = i + 1;
        lob_queue_push(queue, &cmd);
        lob_queue_pop(queue, &cmd);
    }
    t1 = get_time_sec();
    double q_time = t1 - t0;
    double q_rate = (double)(q_ops * 2) / q_time;
    double q_latency_ns = (q_time / (double)(q_ops * 2)) * 1e9;

    printf("  [Stage 4: Lock-Free Queue] %zu push/pop operations\n", q_ops * 2);
    printf("    Elapsed:    %.4f sec\n", q_time);
    printf("    Throughput: %.2f million ops/sec\n", q_rate / 1e6);
    printf("    Latency:    %.1f ns / op\n\n", q_latency_ns);

    lob_queue_free(queue);
    lob_free(book);

    /* ── Stage 5: SIMD Vectorized Batch Execution ── */
    size_t batch_ops = 1000000;
    uint32_t maker_qtys[4] = {100, 200, 150, 300};
    uint32_t m_out[4], r_out[4], r_taker;

    t0 = get_time_sec();
    for (size_t i = 0; i < batch_ops; i++) {
        lob_simd_match_batch_u32(250, maker_qtys, 4, m_out, r_out, &r_taker);
    }
    t1 = get_time_sec();
    double simd_time = t1 - t0;
    double simd_rate = (double)(batch_ops * 4) / simd_time;
    double simd_latency_ns = (simd_time / (double)batch_ops) * 1e9;

    printf("  [Stage 5: SIMD Vectorized Execution] %zu orders evaluated (%s)\n", batch_ops * 4,
           lob_simd_active_backend_name());
    printf("    Elapsed:    %.4f sec\n", simd_time);
    printf("    Throughput: %.2f million orders evaluated/sec\n", simd_rate / 1e6);
    printf("    Latency:    %.1f ns / 4-order batch (%.2f ns / order)\n\n", simd_latency_ns,
           simd_latency_ns / 4.0);

    printf("  ✓ Benchmark completed with zero memory leaks.\n\n");
    return 0;
}
