/* ============================================================================
 * test_order_book.c — Comprehensive Unit Tests for L3 Limit Order Book
 * ============================================================================ */

#include "order_book.h"
#include "test_harness.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- Match callback test helper --- */
typedef struct {
    uint32_t count;
    uint64_t last_maker_id;
    uint64_t last_taker_id;
    uint64_t last_price;
    uint32_t last_qty;
    uint32_t total_matched_qty;
} test_match_tracker_t;

static void on_test_match(const lob_match_t *match, void *user_data) {
    if (!match || !user_data)
        return;
    test_match_tracker_t *t = (test_match_tracker_t *)user_data;
    t->count++;
    t->last_maker_id = match->maker_order_id;
    t->last_taker_id = match->taker_order_id;
    t->last_price = match->price;
    t->last_qty = match->qty;
    t->total_matched_qty += match->qty;
}

/* ----------------------------------------------------------------------------
 * 1. Cache Alignment & Sizing Tests
 * ---------------------------------------------------------------------------- */

void test_lob_cache_alignment(void) {
    ASSERT_EQ(sizeof(lob_order_t), (size_t)64);
    ASSERT_EQ(sizeof(lob_price_level_t), (size_t)64);

    lob_book_t *book = lob_create(16, 16);
    ASSERT_NOT_NULL(book);

    /* Verify pointers from memory pool are 64-byte aligned */
    lob_order_t *ord = (lob_order_t *)book->order_pool.buffer;
    ASSERT_EQ((uintptr_t)ord % 64, (uintptr_t)0);

    lob_price_level_t *lvl = (lob_price_level_t *)book->level_pool.buffer;
    ASSERT_EQ((uintptr_t)lvl % 64, (uintptr_t)0);

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 2. Book Lifecycle & Memory Pool Recycling Tests
 * ---------------------------------------------------------------------------- */

void test_lob_create_and_free(void) {
    lob_book_t *book = lob_create(100, 20);
    ASSERT_NOT_NULL(book);
    ASSERT_EQ(lob_order_count(book), (size_t)0);
    ASSERT_EQ(lob_level_count(book, LOB_SIDE_BUY), (size_t)0);
    ASSERT_EQ(lob_level_count(book, LOB_SIDE_SELL), (size_t)0);

    lob_free(book);
    lob_free(NULL); /* Safe with NULL */

    ASSERT_NULL(lob_create(0, 10));
    ASSERT_NULL(lob_create(10, 0));
}

void test_lob_pool_exhaustion(void) {
    /* Create book with tiny capacity of 2 orders */
    lob_book_t *book = lob_create(2, 2);
    ASSERT_NOT_NULL(book);

    ASSERT_EQ(lob_add_order(book, 1, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_OK);
    ASSERT_EQ(lob_add_order(book, 2, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_OK);

    /* Third order should fail because pool is exhausted */
    ASSERT_EQ(lob_add_order(book, 3, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_POOL_EXHAUSTED);

    /* Cancel order 1, freeing slot back to lock-free pool */
    ASSERT_EQ(lob_cancel_order(book, 1), LOB_RESULT_OK);

    /* Now adding order 3 succeeds */
    ASSERT_EQ(lob_add_order(book, 3, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_OK);

    lob_free(book);

    /* Test price level pool exhaustion */
    lob_book_t *lvl_book = lob_create(10, 1);
    ASSERT_NOT_NULL(lvl_book);
    ASSERT_EQ(lob_add_order(lvl_book, 1, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_OK);
    /* Second level should exhaust level pool */
    ASSERT_EQ(lob_add_order(lvl_book, 2, LOB_SIDE_BUY, 90, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_POOL_EXHAUSTED);
    lob_free(lvl_book);
}

void test_lob_clear(void) {
    lob_book_t *book = lob_create(32, 16);
    ASSERT_NOT_NULL(book);

    lob_add_order(book, 1, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 2, LOB_SIDE_SELL, 105, 15, LOB_TYPE_LIMIT, NULL, NULL);
    ASSERT_EQ(lob_order_count(book), (size_t)2);

    lob_clear(book);
    ASSERT_EQ(lob_order_count(book), (size_t)0);
    ASSERT_EQ(lob_level_count(book, LOB_SIDE_BUY), (size_t)0);
    ASSERT_EQ(lob_level_count(book, LOB_SIDE_SELL), (size_t)0);
    ASSERT_EQ(lob_total_volume(book), (uint64_t)0);

    /* Adding after clear works */
    ASSERT_EQ(lob_add_order(book, 1, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_OK);
    ASSERT_EQ(lob_order_count(book), (size_t)1);

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 3. Basic Order Entry & BBO Tests
 * ---------------------------------------------------------------------------- */

void test_lob_basic_bbo(void) {
    lob_book_t *book = lob_create(64, 32);
    ASSERT_NOT_NULL(book);

    uint64_t bp = 0;
    uint32_t bq = 0;
    uint64_t ap = 0;
    uint32_t aq = 0;

    ASSERT_FALSE(lob_get_bbo(book, &bp, &bq, &ap, &aq));
    ASSERT_EQ(lob_get_spread(book), UINT64_MAX);
    ASSERT_EQ(lob_get_mid_price(book), (uint64_t)0);

    /* Add Buy Order: 100 @ 50 */
    lob_result_t res = lob_add_order(book, 101, LOB_SIDE_BUY, 100, 50, LOB_TYPE_LIMIT, NULL, NULL);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_TRUE(lob_get_bbo(book, &bp, &bq, &ap, &aq));
    ASSERT_EQ(bp, (uint64_t)100);
    ASSERT_EQ(bq, (uint32_t)50);
    ASSERT_EQ(ap, (uint64_t)0);
    ASSERT_EQ(aq, (uint32_t)0);

    /* Add Sell Order: 105 @ 20 */
    res = lob_add_order(book, 102, LOB_SIDE_SELL, 105, 20, LOB_TYPE_LIMIT, NULL, NULL);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_TRUE(lob_get_bbo(book, &bp, &bq, &ap, &aq));
    ASSERT_EQ(bp, (uint64_t)100);
    ASSERT_EQ(bq, (uint32_t)50);
    ASSERT_EQ(ap, (uint64_t)105);
    ASSERT_EQ(aq, (uint32_t)20);

    ASSERT_EQ(lob_get_spread(book), (uint64_t)5);
    ASSERT_EQ(lob_get_mid_price(book), (uint64_t)102);

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 4. Price-Time Priority (FIFO) Tests
 * ---------------------------------------------------------------------------- */

void test_lob_price_time_priority(void) {
    lob_book_t *book = lob_create(64, 32);
    ASSERT_NOT_NULL(book);

    /* Place 3 Sell orders at the same price (100) */
    lob_add_order(book, 1, LOB_SIDE_SELL, 100, 10, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 2, LOB_SIDE_SELL, 100, 20, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 3, LOB_SIDE_SELL, 100, 30, LOB_TYPE_LIMIT, NULL, NULL);

    ASSERT_EQ(lob_order_count(book), (size_t)3);
    ASSERT_EQ(lob_level_count(book, LOB_SIDE_SELL), (size_t)1);

    /* Incoming Buy order for 25 @ 100 should match Order 1 (10) first, then Order 2 (15) */
    test_match_tracker_t tracker = {0, 0, 0, 0, 0, 0};
    lob_result_t res =
        lob_add_order(book, 10, LOB_SIDE_BUY, 100, 25, LOB_TYPE_LIMIT, on_test_match, &tracker);
    ASSERT_EQ(res, LOB_RESULT_OK);

    ASSERT_EQ(tracker.count, (uint32_t)2);
    ASSERT_EQ(tracker.total_matched_qty, (uint32_t)25);

    /* Order 1 is completely filled and deleted */
    ASSERT_NULL(lob_find_order(book, 1));

    /* Order 2 has 5 remaining */
    const lob_order_t *ord2 = lob_find_order(book, 2);
    ASSERT_NOT_NULL(ord2);
    ASSERT_EQ(ord2->qty, (uint32_t)5);
    ASSERT_EQ(ord2->filled_qty, (uint32_t)15);

    /* Order 3 is untouched */
    const lob_order_t *ord3 = lob_find_order(book, 3);
    ASSERT_NOT_NULL(ord3);
    ASSERT_EQ(ord3->qty, (uint32_t)30);

    /* Check remaining level quantity */
    uint64_t bp = 0, ap = 0;
    uint32_t bq = 0, aq = 0;
    lob_get_bbo(book, &bp, &bq, &ap, &aq);
    ASSERT_EQ(ap, (uint64_t)100);
    ASSERT_EQ(aq, (uint32_t)35); /* 5 + 30 */

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 5. Multi-Level Crossing & Depth Tests
 * ---------------------------------------------------------------------------- */

void test_lob_multi_level_matching_and_depth(void) {
    lob_book_t *book = lob_create(128, 64);
    ASSERT_NOT_NULL(book);

    /* Build Asks depth (testing middle insertion):
     * 101 @ 10
     * 103 @ 30
     * 102 @ 20 (inserted between 101 and 103)
     */
    lob_add_order(book, 1, LOB_SIDE_SELL, 101, 10, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 3, LOB_SIDE_SELL, 103, 30, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 2, LOB_SIDE_SELL, 102, 20, LOB_TYPE_LIMIT, NULL, NULL);

    /* Build Bids depth (testing middle insertion):
     * 100 @ 15
     * 80 @ 25
     * 90 @ 10 (inserted between 100 and 80)
     */
    lob_add_order(book, 4, LOB_SIDE_BUY, 100, 15, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 5, LOB_SIDE_BUY, 80, 25, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 6, LOB_SIDE_BUY, 90, 10, LOB_TYPE_LIMIT, NULL, NULL);

    /* Check depth */
    uint64_t ask_prices[5];
    uint64_t ask_qtys[5];
    uint32_t ask_counts[5];
    size_t ask_depth = lob_get_depth(book, LOB_SIDE_SELL, 5, ask_prices, ask_qtys, ask_counts);
    ASSERT_EQ(ask_depth, (size_t)3);
    ASSERT_EQ(ask_prices[0], (uint64_t)101);
    ASSERT_EQ(ask_qtys[0], (uint64_t)10);
    ASSERT_EQ(ask_prices[1], (uint64_t)102);
    ASSERT_EQ(ask_qtys[1], (uint64_t)20);
    ASSERT_EQ(ask_prices[2], (uint64_t)103);
    ASSERT_EQ(ask_qtys[2], (uint64_t)30);

    uint64_t bid_prices[5];
    uint64_t bid_qtys[5];
    uint32_t bid_counts[5];
    size_t bid_depth = lob_get_depth(book, LOB_SIDE_BUY, 5, bid_prices, bid_qtys, bid_counts);
    ASSERT_EQ(bid_depth, (size_t)3);
    ASSERT_EQ(bid_prices[0], (uint64_t)100);
    ASSERT_EQ(bid_prices[1], (uint64_t)90);
    ASSERT_EQ(bid_prices[2], (uint64_t)80);

    /* Aggressive Buy crossing levels 101 and 102 for 35 units @ 102:
     * Fills 10 @ 101, 20 @ 102 -> 30 filled, 5 remainder rests at 102
     */
    test_match_tracker_t tracker = {0, 0, 0, 0, 0, 0};
    lob_result_t res =
        lob_add_order(book, 100, LOB_SIDE_BUY, 102, 35, LOB_TYPE_LIMIT, on_test_match, &tracker);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_EQ(tracker.count, (uint32_t)2);
    ASSERT_EQ(tracker.total_matched_qty, (uint32_t)30);

    /* Resting portion (5 @ 102) becomes new best bid! */
    uint64_t bp = 0, ap = 0;
    uint32_t bq = 0, aq = 0;
    lob_get_bbo(book, &bp, &bq, &ap, &aq);
    ASSERT_EQ(bp, (uint64_t)102);
    ASSERT_EQ(bq, (uint32_t)5);
    ASSERT_EQ(ap, (uint64_t)103);
    ASSERT_EQ(aq, (uint32_t)30);

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 6. Order Cancellation Tests
 * ---------------------------------------------------------------------------- */

void test_lob_cancel_orders(void) {
    lob_book_t *book = lob_create(64, 32);
    ASSERT_NOT_NULL(book);

    /* Add 3 orders at same price */
    lob_add_order(book, 10, LOB_SIDE_BUY, 50, 10, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 20, LOB_SIDE_BUY, 50, 20, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 30, LOB_SIDE_BUY, 50, 30, LOB_TYPE_LIMIT, NULL, NULL);

    /* Cancel middle order (20) */
    ASSERT_EQ(lob_cancel_order(book, 20), LOB_RESULT_OK);
    ASSERT_NULL(lob_find_order(book, 20));
    ASSERT_EQ(lob_order_count(book), (size_t)2);

    /* Cancel head order (10) */
    ASSERT_EQ(lob_cancel_order(book, 10), LOB_RESULT_OK);
    ASSERT_NULL(lob_find_order(book, 10));

    /* Cancel tail order (30) -> price level should be pruned */
    ASSERT_EQ(lob_cancel_order(book, 30), LOB_RESULT_OK);
    ASSERT_NULL(lob_find_order(book, 30));
    ASSERT_EQ(lob_level_count(book, LOB_SIDE_BUY), (size_t)0);
    ASSERT_EQ(lob_order_count(book), (size_t)0);

    /* Cancel non-existent order */
    ASSERT_EQ(lob_cancel_order(book, 9999), LOB_RESULT_ORDER_NOT_FOUND);

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 7. Order Modification Tests
 * ---------------------------------------------------------------------------- */

void test_lob_modify_orders(void) {
    lob_book_t *book = lob_create(64, 32);
    ASSERT_NOT_NULL(book);

    lob_add_order(book, 1, LOB_SIDE_BUY, 100, 20, LOB_TYPE_LIMIT, NULL, NULL);
    lob_add_order(book, 2, LOB_SIDE_BUY, 100, 30, LOB_TYPE_LIMIT, NULL, NULL);

    /* 1. Reduce size: 20 -> 10 (retains time priority over order 2) */
    ASSERT_EQ(lob_modify_order(book, 1, 10, 100, NULL, NULL), LOB_RESULT_OK);
    const lob_order_t *o1 = lob_find_order(book, 1);
    ASSERT_NOT_NULL(o1);
    ASSERT_EQ(o1->qty, (uint32_t)10);

    /* Cross with 15 units sell: order 1 should fill 10 completely, then order 2 fills 5 */
    test_match_tracker_t tracker = {0, 0, 0, 0, 0, 0};
    lob_add_order(book, 100, LOB_SIDE_SELL, 100, 15, LOB_TYPE_LIMIT, on_test_match, &tracker);
    ASSERT_EQ(tracker.count, (uint32_t)2);
    ASSERT_EQ(tracker.last_maker_id, (uint64_t)2);

    /* 2. Modify to qty 0 -> cancels order */
    ASSERT_EQ(lob_modify_order(book, 2, 0, 100, NULL, NULL), LOB_RESULT_OK);
    ASSERT_NULL(lob_find_order(book, 2));

    /* 3. Modify price: moves order to new level */
    lob_add_order(book, 3, LOB_SIDE_SELL, 200, 50, LOB_TYPE_LIMIT, NULL, NULL);
    ASSERT_EQ(lob_modify_order(book, 3, 50, 190, NULL, NULL), LOB_RESULT_OK);
    const lob_order_t *o3 = lob_find_order(book, 3);
    ASSERT_NOT_NULL(o3);
    ASSERT_EQ(o3->price, (uint64_t)190);

    /* Modify non-existent order */
    ASSERT_EQ(lob_modify_order(book, 9999, 10, 100, NULL, NULL), LOB_RESULT_ORDER_NOT_FOUND);

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 8. Order Types (IOC, FOK, Post-Only, Market) Tests
 * ---------------------------------------------------------------------------- */

void test_lob_order_types(void) {
    lob_book_t *book = lob_create(64, 32);
    ASSERT_NOT_NULL(book);

    /* Resting sell order: 100 @ 10 */
    lob_add_order(book, 1, LOB_SIDE_SELL, 100, 10, LOB_TYPE_LIMIT, NULL, NULL);

    /* IOC: Buy 25 @ 100. Should fill 10 immediately, cancel remaining 15 without resting */
    test_match_tracker_t tracker = {0, 0, 0, 0, 0, 0};
    lob_result_t res =
        lob_add_order(book, 2, LOB_SIDE_BUY, 100, 25, LOB_TYPE_IOC, on_test_match, &tracker);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_EQ(tracker.total_matched_qty, (uint32_t)10);
    ASSERT_NULL(lob_find_order(book, 2)); /* Not resting */
    ASSERT_EQ(lob_order_count(book), (size_t)0);

    /* Setup liquidity for FOK */
    lob_add_order(book, 10, LOB_SIDE_SELL, 100, 20, LOB_TYPE_LIMIT, NULL, NULL);

    /* FOK requiring 30 @ 100 should be rejected because only 20 available */
    tracker.count = 0;
    tracker.total_matched_qty = 0;
    res = lob_add_order(book, 20, LOB_SIDE_BUY, 100, 30, LOB_TYPE_FOK, on_test_match, &tracker);
    ASSERT_EQ(res, LOB_RESULT_FOK_NOT_FILLED);
    ASSERT_EQ(tracker.count, (uint32_t)0);
    ASSERT_EQ(lob_order_count(book), (size_t)1); /* Order 10 intact */

    /* FOK requiring 15 @ 100 should succeed and fill completely */
    res = lob_add_order(book, 21, LOB_SIDE_BUY, 100, 15, LOB_TYPE_FOK, on_test_match, &tracker);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_EQ(tracker.total_matched_qty, (uint32_t)15);

    /* Test Sell FOK against resting Bids:
     * Add Buy liquidity: 30 @ 50
     * Sell FOK: 40 @ 50 -> rejected
     * Sell FOK: 20 @ 50 -> succeeds
     */
    lob_add_order(book, 25, LOB_SIDE_BUY, 50, 30, LOB_TYPE_LIMIT, NULL, NULL);
    res = lob_add_order(book, 26, LOB_SIDE_SELL, 50, 40, LOB_TYPE_FOK, NULL, NULL);
    ASSERT_EQ(res, LOB_RESULT_FOK_NOT_FILLED);
    res = lob_add_order(book, 27, LOB_SIDE_SELL, 50, 20, LOB_TYPE_FOK, NULL, NULL);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_EQ(lob_trade_count(book) > 0, true);

    /* Post-Only:
     * Remaining book has 5 @ 100 (Sell).
     * Post-Only Buy @ 100 would cross and must be rejected.
     */
    res = lob_add_order(book, 30, LOB_SIDE_BUY, 100, 5, LOB_TYPE_POST_ONLY, NULL, NULL);
    ASSERT_EQ(res, LOB_RESULT_POST_ONLY_CROSSED);

    /* Post-Only Buy @ 99 does not cross and rests */
    res = lob_add_order(book, 31, LOB_SIDE_BUY, 99, 5, LOB_TYPE_POST_ONLY, NULL, NULL);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_NOT_NULL(lob_find_order(book, 31));

    /* Market Order: Sell 5 @ market executes against best bid (99) */
    tracker.count = 0;
    tracker.total_matched_qty = 0;
    res = lob_add_order(book, 40, LOB_SIDE_SELL, 0, 5, LOB_TYPE_MARKET, on_test_match, &tracker);
    ASSERT_EQ(res, LOB_RESULT_OK);
    ASSERT_EQ(tracker.total_matched_qty, (uint32_t)5);
    ASSERT_EQ(tracker.last_price, (uint64_t)99);

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 9. Input Validation & Error Handling Tests
 * ---------------------------------------------------------------------------- */

void test_lob_validation_and_errors(void) {
    lob_book_t *book = lob_create(32, 16);
    ASSERT_NOT_NULL(book);

    /* Duplicate order ID */
    ASSERT_EQ(lob_add_order(book, 1, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_OK);
    ASSERT_EQ(lob_add_order(book, 1, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_DUPLICATE_ORDER_ID);

    /* Invalid arguments */
    ASSERT_EQ(lob_add_order(NULL, 2, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_INVALID_ARG);
    ASSERT_EQ(lob_add_order(book, 0, LOB_SIDE_BUY, 100, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_INVALID_ARG);
    ASSERT_EQ(lob_add_order(book, 2, LOB_SIDE_BUY, 100, 0, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_INVALID_ARG);
    ASSERT_EQ(lob_add_order(book, 2, LOB_SIDE_BUY, 0, 10, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_INVALID_ARG);

    ASSERT_EQ(lob_cancel_order(NULL, 1), LOB_RESULT_INVALID_ARG);
    ASSERT_EQ(lob_cancel_order(book, 0), LOB_RESULT_INVALID_ARG);

    ASSERT_EQ(lob_modify_order(NULL, 1, 5, 100, NULL, NULL), LOB_RESULT_INVALID_ARG);
    ASSERT_EQ(lob_modify_order(book, 0, 5, 100, NULL, NULL), LOB_RESULT_INVALID_ARG);

    ASSERT_NULL(lob_find_order(NULL, 1));
    ASSERT_NULL(lob_find_order(book, 0));
    ASSERT_NULL(lob_find_order(book, 99999));

    lob_free(book);
}

/* ----------------------------------------------------------------------------
 * 10. Lock-Free Command Queue (MPMC) Tests
 * ---------------------------------------------------------------------------- */

void test_lob_command_queue(void) {
    /* Capacity 4 slots */
    lob_queue_t *q = lob_queue_create(4);
    ASSERT_NOT_NULL(q);

    lob_command_t cmd_out;
    ASSERT_FALSE(lob_queue_pop(q, &cmd_out)); /* Empty */

    /* Push 4 commands */
    for (uint64_t i = 1; i <= 4; i++) {
        lob_command_t c;
        c.type = LOB_CMD_ADD;
        c.side = LOB_SIDE_BUY;
        c.order_type = LOB_TYPE_LIMIT;
        c._pad = 0;
        c.qty = 10;
        c.order_id = i;
        c.price = 100 + i;
        ASSERT_TRUE(lob_queue_push(q, &c));
    }

    /* 5th command should return false (full) */
    lob_command_t c5 = {LOB_CMD_ADD, LOB_SIDE_BUY, LOB_TYPE_LIMIT, 0, 10, 5, 105};
    ASSERT_FALSE(lob_queue_push(q, &c5));

    /* Pop 2 commands */
    ASSERT_TRUE(lob_queue_pop(q, &cmd_out));
    ASSERT_EQ(cmd_out.order_id, (uint64_t)1);
    ASSERT_TRUE(lob_queue_pop(q, &cmd_out));
    ASSERT_EQ(cmd_out.order_id, (uint64_t)2);

    /* Push 2 more commands to test ring wraparound (including modify command) */
    ASSERT_TRUE(lob_queue_push(q, &c5));
    lob_command_t c6 = {LOB_CMD_MODIFY, 0, 0, 0, 5, 4, 104};
    ASSERT_TRUE(lob_queue_push(q, &c6));

    /* Now process queue into a live order book */
    lob_book_t *book = lob_create(32, 16);
    ASSERT_NOT_NULL(book);

    size_t processed = lob_process_queue(book, q, 0, NULL, NULL);
    ASSERT_EQ(processed, (size_t)4);

    /* Command 4 was added with qty 10, then modified to qty 5 */
    const lob_order_t *o4 = lob_find_order(book, 4);
    ASSERT_NOT_NULL(o4);
    ASSERT_EQ(o4->qty, (uint32_t)5);
    ASSERT_NOT_NULL(lob_find_order(book, 5));

    lob_free(book);
    lob_queue_free(q);
    lob_queue_free(NULL);
}
