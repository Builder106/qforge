/* ============================================================================
 * test_simd_match.c — Unit Tests for Vectorized SIMD Matching Engine
 * ============================================================================ */

#include "order_book.h"
#include "simd_match.h"
#include "test_harness.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- Match callback tracker --- */
typedef struct {
    uint32_t count;
    uint64_t last_maker_id;
    uint64_t last_taker_id;
    uint64_t last_price;
    uint32_t last_qty;
    uint32_t total_matched_qty;
} simd_test_match_tracker_t;

static void on_simd_test_match(const lob_match_t *match, void *user_data) {
    if (!match || !user_data) {
        return;
    }
    simd_test_match_tracker_t *t = (simd_test_match_tracker_t *)user_data;
    t->count++;
    t->last_maker_id = match->maker_order_id;
    t->last_taker_id = match->taker_order_id;
    t->last_price = match->price;
    t->last_qty = match->qty;
    t->total_matched_qty += match->qty;
}

/* ----------------------------------------------------------------------------
 * 1. Backend Detection Tests
 * ---------------------------------------------------------------------------- */

void test_simd_backend_detection(void) {
    lob_simd_backend_t backend = lob_simd_get_backend();
    ASSERT_TRUE(backend >= LOB_SIMD_BACKEND_PORTABLE && backend <= LOB_SIMD_BACKEND_NEON);

    const char *name = lob_simd_active_backend_name();
    ASSERT_NOT_NULL(name);
    ASSERT_TRUE(strlen(name) > 0);

    /* Verify named mapping */
    ASSERT_EQ(strcmp(lob_simd_backend_name(LOB_SIMD_BACKEND_PORTABLE), "Portable C99"), 0);
    ASSERT_EQ(strcmp(lob_simd_backend_name(LOB_SIMD_BACKEND_NEON), "ARM NEON"), 0);
    ASSERT_EQ(strcmp(lob_simd_backend_name(LOB_SIMD_BACKEND_AVX2), "AVX2 (256-bit)"), 0);
    ASSERT_EQ(strcmp(lob_simd_backend_name(LOB_SIMD_BACKEND_SSE42), "SSE4.2 (128-bit)"), 0);
}

/* ----------------------------------------------------------------------------
 * 2. Vectorized Batch Matching: Basic Scenarios
 * ---------------------------------------------------------------------------- */

void test_simd_batch_matching_basic(void) {
    /* Exact fill across 4 resting orders */
    uint32_t makers[4] = {10, 20, 30, 40};
    uint32_t matched[4] = {0};
    uint32_t rem_makers[4] = {0};
    uint32_t rem_taker = 0;

    uint32_t total = lob_simd_match_batch_u32(100, makers, 4, matched, rem_makers, &rem_taker);
    ASSERT_EQ(total, 100U);
    ASSERT_EQ(rem_taker, 0U);
    ASSERT_EQ(matched[0], 10U);
    ASSERT_EQ(matched[1], 20U);
    ASSERT_EQ(matched[2], 30U);
    ASSERT_EQ(matched[3], 40U);
    ASSERT_EQ(rem_makers[0], 0U);
    ASSERT_EQ(rem_makers[1], 0U);
    ASSERT_EQ(rem_makers[2], 0U);
    ASSERT_EQ(rem_makers[3], 0U);

    /* Partial fill: taker_qty = 25 -> orders 0, 1 filled; order 2 gets 5; order 3 untouched */
    memset(matched, 0, sizeof(matched));
    memset(rem_makers, 0, sizeof(rem_makers));
    total = lob_simd_match_batch_u32(25, makers, 4, matched, rem_makers, &rem_taker);
    ASSERT_EQ(total, 25U);
    ASSERT_EQ(rem_taker, 0U);
    ASSERT_EQ(matched[0], 10U);
    ASSERT_EQ(matched[1], 15U);
    ASSERT_EQ(matched[2], 0U);
    ASSERT_EQ(matched[3], 0U);
    ASSERT_EQ(rem_makers[0], 0U);
    ASSERT_EQ(rem_makers[1], 5U);
    ASSERT_EQ(rem_makers[2], 30U);
    ASSERT_EQ(rem_makers[3], 40U);

    /* Taker larger than total maker quantity: taker_qty = 150 */
    memset(matched, 0, sizeof(matched));
    memset(rem_makers, 0, sizeof(rem_makers));
    total = lob_simd_match_batch_u32(150, makers, 4, matched, rem_makers, &rem_taker);
    ASSERT_EQ(total, 100U);
    ASSERT_EQ(rem_taker, 50U);
    ASSERT_EQ(matched[0], 10U);
    ASSERT_EQ(matched[1], 20U);
    ASSERT_EQ(matched[2], 30U);
    ASSERT_EQ(matched[3], 40U);

    /* Zero taker quantity */
    memset(matched, 0, sizeof(matched));
    memset(rem_makers, 0, sizeof(rem_makers));
    total = lob_simd_match_batch_u32(0, makers, 4, matched, rem_makers, &rem_taker);
    ASSERT_EQ(total, 0U);
    ASSERT_EQ(rem_taker, 0U);
    ASSERT_EQ(matched[0], 0U);
    ASSERT_EQ(rem_makers[0], 10U);
    ASSERT_EQ(rem_makers[1], 20U);
    ASSERT_EQ(rem_makers[2], 30U);
    ASSERT_EQ(rem_makers[3], 40U);
}

/* ----------------------------------------------------------------------------
 * 3. Batch Matching Permutations & Portable Equivalence
 * ---------------------------------------------------------------------------- */

void test_simd_batch_matching_permutations(void) {
    uint32_t makers[16];
    for (uint32_t i = 0; i < 16; i++) {
        makers[i] = (i + 1) * 7;
    }

    uint32_t m_active[16], rem_active[16], rem_taker_active;
    uint32_t m_port[16], rem_port[16], rem_taker_port;

    for (uint32_t count = 1; count <= 16; count++) {
        for (uint32_t taker = 0; taker <= 600; taker += 13) {
            memset(m_active, 0, sizeof(m_active));
            memset(rem_active, 0, sizeof(rem_active));
            memset(m_port, 0, sizeof(m_port));
            memset(rem_port, 0, sizeof(rem_port));

            uint32_t tot_active = lob_simd_match_batch_u32(taker, makers, count, m_active,
                                                           rem_active, &rem_taker_active);
            uint32_t tot_port = lob_simd_match_batch_u32_portable(taker, makers, count, m_port,
                                                                  rem_port, &rem_taker_port);

            ASSERT_EQ(tot_active, tot_port);
            ASSERT_EQ(rem_taker_active, rem_taker_port);

            for (uint32_t k = 0; k < count; k++) {
                ASSERT_EQ(m_active[k], m_port[k]);
                ASSERT_EQ(rem_active[k], rem_port[k]);
            }
        }
    }
}

/* ----------------------------------------------------------------------------
 * 4. Saturating Arithmetic & Overflow Resistance
 * ---------------------------------------------------------------------------- */

void test_simd_batch_matching_overflow(void) {
    /* Quantities that exceed 2^32 if summed without saturating arithmetic */
    uint32_t huge_makers[4] = {2000000000U, 2000000000U, 2000000000U, 2000000000U};
    uint32_t matched[4] = {0};
    uint32_t rem[4] = {0};
    uint32_t rem_taker = 0;

    uint32_t total =
        lob_simd_match_batch_u32(3000000000U, huge_makers, 4, matched, rem, &rem_taker);
    ASSERT_EQ(total, 3000000000U);
    ASSERT_EQ(rem_taker, 0U);
    ASSERT_EQ(matched[0], 2000000000U);
    ASSERT_EQ(matched[1], 1000000000U);
    ASSERT_EQ(matched[2], 0U);
    ASSERT_EQ(matched[3], 0U);
    ASSERT_EQ(rem[0], 0U);
    ASSERT_EQ(rem[1], 1000000000U);
    ASSERT_EQ(rem[2], 2000000000U);
    ASSERT_EQ(rem[3], 2000000000U);
}

/* ----------------------------------------------------------------------------
 * 5. Vectorized Price Crossing Comparison
 * ---------------------------------------------------------------------------- */

void test_simd_price_crossing(void) {
    /* BUY taker: limit_price >= ask_price */
    uint64_t ask_prices[8] = {100, 102, 104, 106, 108, 110, 112, 114};
    uint32_t mask = 0;

    /* limit_price = 105 crosses 100, 102, 104 (3 levels) */
    uint32_t count = lob_simd_compare_prices_u64(105, ask_prices, 8, LOB_SIDE_BUY, &mask);
    ASSERT_EQ(count, 3U);
    ASSERT_EQ(mask, 0x7U);

    /* limit_price = 99 crosses 0 levels */
    count = lob_simd_compare_prices_u64(99, ask_prices, 8, LOB_SIDE_BUY, &mask);
    ASSERT_EQ(count, 0U);
    ASSERT_EQ(mask, 0x0U);

    /* limit_price = 120 crosses all 8 levels */
    count = lob_simd_compare_prices_u64(120, ask_prices, 8, LOB_SIDE_BUY, &mask);
    ASSERT_EQ(count, 8U);
    ASSERT_EQ(mask, 0xFFU);

    /* SELL taker: limit_price <= bid_price (descending bids) */
    uint64_t bid_prices[8] = {114, 112, 110, 108, 106, 104, 102, 100};
    /* limit_price = 109 crosses 114, 112, 110 (3 levels) */
    count = lob_simd_compare_prices_u64(109, bid_prices, 8, LOB_SIDE_SELL, &mask);
    ASSERT_EQ(count, 3U);
    ASSERT_EQ(mask, 0x7U);

    /* Equivalence against portable reference */
    for (uint64_t p = 95; p <= 120; p++) {
        uint32_t m_act = 0, m_ref = 0;
        uint32_t c_act = lob_simd_compare_prices_u64(p, ask_prices, 8, LOB_SIDE_BUY, &m_act);
        uint32_t c_ref =
            lob_simd_compare_prices_u64_portable(p, ask_prices, 8, LOB_SIDE_BUY, &m_ref);
        ASSERT_EQ(c_act, c_ref);
        ASSERT_EQ(m_act, m_ref);
    }
}

/* ----------------------------------------------------------------------------
 * 6. Vectorized Depth & Volume Sweep
 * ---------------------------------------------------------------------------- */

void test_simd_volume_accumulation(void) {
    uint64_t prices[6] = {100, 105, 110, 115, 120, 125};
    uint64_t volumes[6] = {50, 75, 100, 150, 200, 250};
    uint32_t checked = 0;

    /* Buy limit at 112 crosses 100, 105, 110 (volumes 50 + 75 + 100 = 225) */
    uint64_t total =
        lob_simd_accumulate_volume_u64(prices, volumes, 6, 112, LOB_SIDE_BUY, 0, &checked);
    ASSERT_EQ(total, 225ULL);
    ASSERT_EQ(checked, 3U); /* 3 levels crossed (100, 105, 110) */

    /* Early threshold stop: min_qty = 100 stops after 100 and 105 (50 + 75 = 125 >= 100) */
    total = lob_simd_accumulate_volume_u64(prices, volumes, 6, 112, LOB_SIDE_BUY, 100, &checked);
    ASSERT_TRUE(total >= 100ULL);
    ASSERT_EQ(checked, 2U);

    /* Equivalence with portable reference */
    for (uint64_t p = 90; p <= 130; p += 5) {
        uint32_t chk_act = 0, chk_ref = 0;
        uint64_t tot_act =
            lob_simd_accumulate_volume_u64(prices, volumes, 6, p, LOB_SIDE_BUY, 150, &chk_act);
        uint64_t tot_ref = lob_simd_accumulate_volume_u64_portable(prices, volumes, 6, p,
                                                                   LOB_SIDE_BUY, 150, &chk_ref);
        ASSERT_EQ(tot_act, tot_ref);
        ASSERT_EQ(chk_act, chk_ref);
    }
}

/* ----------------------------------------------------------------------------
 * 7. Multi-Order SIMD Level Matching in Book
 * ---------------------------------------------------------------------------- */

void test_simd_level_matching(void) {
    lob_book_t *book = lob_create(64, 32);
    ASSERT_NOT_NULL(book);

    /* Add 6 resting sell orders at the same price level 50000 */
    for (uint64_t i = 1; i <= 6; i++) {
        lob_result_t res = lob_add_order(book, i, LOB_SIDE_SELL, 50000, 10 * (uint32_t)i,
                                         LOB_TYPE_LIMIT, NULL, NULL);
        ASSERT_EQ(res, LOB_RESULT_OK);
    }
    ASSERT_EQ(lob_order_count(book), (size_t)6);
    ASSERT_EQ(lob_level_count(book, LOB_SIDE_SELL), (size_t)1);
    /* Total quantity = 10 + 20 + 30 + 40 + 50 + 60 = 210 */

    /* Incoming BUY order for quantity 75:
     * Should consume order 1 (10), order 2 (20), order 3 (30) completely (60 total),
     * and 15 from order 4 (resting qty becomes 40 - 15 = 25). */
    simd_test_match_tracker_t tracker = {0};
    lob_result_t res = lob_add_order(book, 100, LOB_SIDE_BUY, 50000, 75, LOB_TYPE_LIMIT,
                                     on_simd_test_match, &tracker);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_EQ(tracker.count, 4U);
    ASSERT_EQ(tracker.total_matched_qty, 75U);

    /* 3 orders fully filled and recycled; 3 remain (orders 4, 5, 6) */
    ASSERT_EQ(lob_order_count(book), (size_t)3);
    ASSERT_NULL(lob_find_order(book, 1));
    ASSERT_NULL(lob_find_order(book, 2));
    ASSERT_NULL(lob_find_order(book, 3));

    const lob_order_t *ord4 = lob_find_order(book, 4);
    ASSERT_NOT_NULL(ord4);
    ASSERT_EQ(ord4->qty, 25U);
    ASSERT_EQ(ord4->filled_qty, 15U);

    const lob_order_t *ord5 = lob_find_order(book, 5);
    ASSERT_NOT_NULL(ord5);
    ASSERT_EQ(ord5->qty, 50U);

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 8. SIMD FOK Pre-check & Multi-Level Sweep
 * ---------------------------------------------------------------------------- */

void test_simd_fok_and_market_sweep(void) {
    lob_book_t *book = lob_create(64, 32);
    ASSERT_NOT_NULL(book);

    /* Add resting bids across 4 price levels:
     * 100: 50
     * 95:  75
     * 90:  100
     * 85:  150
     * Total volume = 375 */
    lob_add_order(book, 1, LOB_SIDE_BUY, 100, 50, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 2, LOB_SIDE_BUY, 95, 75, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 3, LOB_SIDE_BUY, 90, 100, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 4, LOB_SIDE_BUY, 85, 150, LOB_TYPE_LIMIT, NULL, NULL);

    /* FOK sell order at price 90 for qty 250:
     * Available at >= 90 is 50 + 75 + 100 = 225 < 250 -> must reject */
    lob_result_t res = lob_add_order(book, 10, LOB_SIDE_SELL, 90, 250, LOB_TYPE_FOK, NULL, NULL);
    ASSERT_EQ(res, LOB_RESULT_FOK_NOT_FILLED);
    ASSERT_EQ(lob_order_count(book), (size_t)4);

    /* FOK sell order at price 90 for qty 200:
     * Available is 225 >= 200 -> must fill completely */
    simd_test_match_tracker_t tracker = {0};
    res =
        lob_add_order(book, 11, LOB_SIDE_SELL, 90, 200, LOB_TYPE_FOK, on_simd_test_match, &tracker);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_EQ(tracker.total_matched_qty, 200U);
    ASSERT_EQ(tracker.count, 3U); /* matched against levels 100, 95, 90 */

    /* Market order sweeping remaining liquidity */
    tracker = (simd_test_match_tracker_t){0};
    res = lob_add_order(book, 12, LOB_SIDE_SELL, 0, 500, LOB_TYPE_MARKET, on_simd_test_match,
                        &tracker);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_EQ(tracker.total_matched_qty, 175U); /* Remaining 25 at 90 + 150 at 85 */
    ASSERT_EQ(lob_order_count(book), (size_t)0);

    lob_free(book);
}
