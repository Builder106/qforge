#include "risk_engine.h"
#include "test_harness.h"

static risk_limits_t limits(void) {
    risk_limits_t value = {100, 10000, 100, 5000, 2500};
    return value;
}

void test_risk_validation(void) {
    risk_limits_t l = limits();
    risk_state_t s;
    ASSERT_NULL(risk_create(NULL, 4, 1000, 100));
    ASSERT_NULL(risk_create(&l, 0, 1000, 100));
    ASSERT_NULL(risk_create(&l, SIZE_MAX, 1000, 100));
    ASSERT_NULL(risk_create(&l, 4, -1, 100));
    ASSERT_NULL(risk_create(&l, 4, 1000, 0));
    ASSERT_NULL(risk_create(&l, 4, 1000, UINT64_MAX));
    l.initial_margin_bps = 10001;
    ASSERT_NULL(risk_create(&l, 4, 1000, 100));
    l = limits();
    l.maintenance_margin_bps = 5001;
    ASSERT_NULL(risk_create(&l, 4, 1000, 100));
    l = limits();
    l.maintenance_margin_bps = 0;
    ASSERT_NULL(risk_create(&l, 4, 1000, 100));
    l = limits();
    l.max_order_qty = 0;
    ASSERT_NULL(risk_create(&l, 4, 1000, 100));
    l = limits();
    l.max_order_notional = 0;
    ASSERT_NULL(risk_create(&l, 4, 1000, 100));
    l = limits();
    l.max_order_notional = UINT64_MAX;
    ASSERT_NULL(risk_create(&l, 4, 1000, 100));
    l = limits();
    l.max_abs_position = 0;
    ASSERT_NULL(risk_create(&l, 4, 1000, 100));
    l = limits();
    l.max_abs_position = UINT64_MAX;
    ASSERT_NULL(risk_create(&l, 4, 1000, 100));
    ASSERT_EQ(risk_reserve(NULL, 1, LOB_SIDE_BUY, 100, 1), RISK_INVALID_ARG);
    ASSERT_EQ(risk_fill(NULL, 1, 100, 1), RISK_INVALID_ARG);
    ASSERT_EQ(risk_release(NULL, 1), RISK_INVALID_ARG);
    ASSERT_EQ(risk_mark(NULL, 100), RISK_INVALID_ARG);
    ASSERT_EQ(risk_set_halted(NULL, true), RISK_INVALID_ARG);
    ASSERT_EQ(risk_get_state(NULL, &s), RISK_INVALID_ARG);
    risk_free(NULL);
}

void test_risk_order_controls(void) {
    risk_limits_t l = limits();
    risk_engine_t *e = risk_create(&l, 2, 10000, 100);
    risk_state_t before, after;
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_get_state(e, NULL), RISK_INVALID_ARG);
    ASSERT_EQ(risk_reserve(e, 0, LOB_SIDE_BUY, 100, 1), RISK_INVALID_ARG);
    ASSERT_EQ(risk_reserve(e, 1, (lob_side_t)2, 100, 1), RISK_INVALID_ARG);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 0, 1), RISK_INVALID_ARG);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 0), RISK_INVALID_ARG);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 101), RISK_ORDER_QTY);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 101, 100), RISK_ORDER_NOTIONAL);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 100), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &before), RISK_OK);
    ASSERT_TRUE(before.initial_margin == 5000 && before.reserved_notional == 10000);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_SELL, 100, 1), RISK_DUPLICATE_ORDER);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_BUY, 100, 1), RISK_POSITION_LIMIT);
    ASSERT_EQ(risk_get_state(e, &after), RISK_OK);
    ASSERT_TRUE(after.reserved_notional == before.reserved_notional);
    ASSERT_TRUE(after.open_orders == before.open_orders);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_SELL, 100, 100), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 3, LOB_SIDE_SELL, 100, 1), RISK_CAPACITY);
    ASSERT_EQ(risk_release(e, 1), RISK_OK);
    ASSERT_EQ(risk_release(e, 1), RISK_ORDER_NOT_FOUND);
    ASSERT_EQ(risk_reserve(e, 3, LOB_SIDE_SELL, 100, 1), RISK_POSITION_LIMIT);
    ASSERT_EQ(risk_reserve(e, 3, LOB_SIDE_BUY, 100, 1), RISK_OK);
    risk_free(e);
}

void test_risk_margin_reservations(void) {
    risk_limits_t l = limits();
    risk_engine_t *e = risk_create(&l, 4, 1000, 100);
    risk_state_t s;
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 20), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_SELL, 100, 1), RISK_MARGIN);
    ASSERT_EQ(risk_release(e, 1), RISK_OK);
    /* Low sell limits cannot under-reserve exposure valued at the mark. */
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_SELL, 1, 21), RISK_MARGIN);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_SELL, 100, 20), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.reserved_notional == 2000 && s.initial_margin == 1000);
    ASSERT_EQ(risk_release(e, 2), RISK_OK);
    ASSERT_EQ(risk_mark(e, 0), RISK_INVALID_ARG);
    ASSERT_EQ(risk_mark(e, UINT64_MAX), RISK_INVALID_ARG);
    ASSERT_EQ(risk_reserve(e, 3, LOB_SIDE_BUY, 100, 1), RISK_OK);
    ASSERT_EQ(risk_mark(e, 200), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.reserved_notional == 200 && s.initial_margin == 100);
    risk_free(e);
    l.initial_margin_bps = 1;
    l.maintenance_margin_bps = 1;
    e = risk_create(&l, 1, 0, 1);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 1, 1), RISK_MARGIN);
    risk_free(e);
    e = risk_create(&l, 1, 1, 1);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 1, 1), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.initial_margin == 1);
    risk_free(e);
}

void test_risk_execution_loss_reservation(void) {
    risk_limits_t l = limits();
    risk_engine_t *e = risk_create(&l, 4, 1000, 1);
    risk_state_t s;
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 20), RISK_MARGIN);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 6), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.reserved_loss == 594 && s.initial_margin == 300);
    ASSERT_EQ(risk_fill(e, 1, 100, 6), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.equity == 406 && !s.maintenance_breached && s.reserved_loss == 0);
    risk_free(e);
    e = risk_create(&l, 4, 1000, 100);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_SELL, 1, 20), RISK_MARGIN);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_SELL, 1, 6), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.reserved_loss == 594 && s.initial_margin == 300);
    ASSERT_EQ(risk_mark(e, 200), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.reserved_loss == 1194 && s.initial_margin == 600);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_BUY, 200, 1), RISK_MARGIN);
    ASSERT_EQ(risk_fill(e, 1, 1, 6), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.equity == -194 && s.maintenance_breached);
    risk_free(e);
}

void test_risk_position_extremes(void) {
    risk_limits_t l = limits();
    risk_state_t s;
    risk_engine_t *e;
    l.max_abs_position = 10;
    e = risk_create(&l, 8, 10000, 100);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 10), RISK_OK);
    ASSERT_EQ(risk_fill(e, 1, 100, 4), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_BUY, 100, 1), RISK_POSITION_LIMIT);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_SELL, 100, 10), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 3, LOB_SIDE_SELL, 100, 5), RISK_POSITION_LIMIT);
    ASSERT_EQ(risk_fill(e, 1, 100, 6), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 3, LOB_SIDE_BUY, 100, 1), RISK_POSITION_LIMIT);
    ASSERT_EQ(risk_fill(e, 2, 100, 10), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 3, LOB_SIDE_SELL, 100, 10), RISK_OK);
    ASSERT_EQ(risk_fill(e, 3, 100, 10), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 4, LOB_SIDE_SELL, 100, 1), RISK_POSITION_LIMIT);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.position == -10 && s.open_orders == 0);
    risk_free(e);
}

void test_risk_maintenance_boundary(void) {
    risk_limits_t l = limits();
    risk_state_t s;
    risk_engine_t *e = risk_create(&l, 4, 1100, 100);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 20), RISK_OK);
    ASSERT_EQ(risk_fill(e, 1, 100, 20), RISK_OK);
    ASSERT_EQ(risk_mark(e, 60), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.equity == 300 && s.maintenance_margin == 300 && !s.maintenance_breached);
    ASSERT_EQ(risk_mark(e, 59), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.equity == 280 && s.maintenance_margin == 295 && s.maintenance_breached);
    risk_free(e);
}

void test_risk_fill_lifecycle(void) {
    risk_limits_t l = limits();
    risk_engine_t *e = risk_create(&l, 4, 1000, 100);
    risk_state_t s;
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 10), RISK_OK);
    ASSERT_EQ(risk_fill(e, 1, 101, 1), RISK_INVALID_ARG);
    ASSERT_EQ(risk_fill(e, 1, 0, 1), RISK_INVALID_ARG);
    ASSERT_EQ(risk_fill(e, 1, 100, 0), RISK_INVALID_ARG);
    ASSERT_EQ(risk_fill(e, 1, 100, 11), RISK_INVALID_ARG);
    ASSERT_EQ(risk_fill(e, 99, 100, 1), RISK_ORDER_NOT_FOUND);
    ASSERT_EQ(risk_fill(e, 1, 90, 4), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.cash == 640 && s.position == 4 && s.equity == 1040);
    ASSERT_TRUE(s.reserved_notional == 600 && s.initial_margin == 500);
    ASSERT_TRUE(s.maintenance_margin == 100 && s.open_orders == 1);
    ASSERT_EQ(risk_release(e, 1), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_SELL, 100, 10), RISK_OK);
    ASSERT_EQ(risk_fill(e, 2, 99, 1), RISK_INVALID_ARG);
    ASSERT_EQ(risk_fill(e, 2, 110, 10), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.cash == 1740 && s.position == -6 && s.equity == 1140);
    ASSERT_TRUE(s.open_orders == 0 && s.reserved_notional == 0);
    ASSERT_EQ(risk_fill(e, 2, 110, 1), RISK_ORDER_NOT_FOUND);
    ASSERT_EQ(risk_mark(e, 120), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.equity == 1020 && s.maintenance_margin == 180);
    risk_free(e);
}

void test_risk_margin_breach_and_halt(void) {
    risk_limits_t l = limits();
    risk_engine_t *e = risk_create(&l, 4, 1000, 100);
    risk_state_t s;
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 20), RISK_OK);
    ASSERT_EQ(risk_fill(e, 1, 100, 20), RISK_OK);
    ASSERT_EQ(risk_mark(e, 50), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.equity == 0 && s.maintenance_breached);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_BUY, 50, 1), RISK_MARGIN);
    ASSERT_EQ(risk_mark(e, 100), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_SELL, 100, 1), RISK_MARGIN);
    ASSERT_EQ(risk_mark(e, 200), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_SELL, 200, 1), RISK_OK);
    ASSERT_EQ(risk_set_halted(e, true), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 3, LOB_SIDE_BUY, 200, 1), RISK_HALTED);
    ASSERT_EQ(risk_fill(e, 2, 200, 1), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.halted && !s.maintenance_breached);
    ASSERT_EQ(risk_set_halted(e, false), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 3, LOB_SIDE_BUY, 200, 1), RISK_OK);
    ASSERT_EQ(risk_set_halted(e, true), RISK_OK);
    ASSERT_EQ(risk_release(e, 3), RISK_OK);
    risk_free(e);
    /* Short marks can cause negative equity; fills still have to settle. */
    e = risk_create(&l, 4, 1000, 100);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_SELL, 100, 20), RISK_OK);
    ASSERT_EQ(risk_fill(e, 1, 100, 19), RISK_OK);
    ASSERT_EQ(risk_mark(e, 200), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.equity == -900 && s.maintenance_breached);
    ASSERT_EQ(risk_fill(e, 1, 100, 1), RISK_OK);
    risk_free(e);
}

void test_risk_overflow(void) {
    risk_limits_t l = {UINT32_MAX, INT64_MAX, INT64_MAX, 10000, 10000};
    risk_engine_t *e = risk_create(&l, 4, INT64_MAX, 1);
    risk_state_t s;
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(risk_mark(e, INT64_MAX), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, INT64_MAX, 2), RISK_OVERFLOW);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, INT64_MAX, 1), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_SELL, 1, 1), RISK_OVERFLOW);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.open_orders == 1 && s.initial_margin == INT64_MAX);
    ASSERT_EQ(risk_release(e, 1), RISK_OK);
    ASSERT_EQ(risk_mark(e, 1), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_SELL, 1, 1), RISK_OK);
    ASSERT_EQ(risk_fill(e, 1, 1, 1), RISK_OVERFLOW);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.position == 0 && s.cash == INT64_MAX && s.open_orders == 1);
    ASSERT_EQ(risk_release(e, 1), RISK_OK);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 1, 2), RISK_OK);
    ASSERT_EQ(risk_fill(e, 1, 1, 2), RISK_OK);
    ASSERT_EQ(risk_mark(e, INT64_MAX), RISK_OVERFLOW);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OVERFLOW);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_BUY, 1, 1), RISK_OVERFLOW);
    ASSERT_EQ(risk_mark(e, 1), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.position == 2 && s.equity == INT64_MAX);
    risk_free(e);
}

static void apply_risk_fill(const lob_match_t *match, void *data) {
    risk_result_t result =
        risk_fill((risk_engine_t *)data, match->taker_order_id, match->price, match->qty);
    if (result != RISK_OK)
        test_failures++;
}

void test_risk_book_integration(void) {
    risk_limits_t l = limits();
    risk_engine_t *e = risk_create(&l, 8, 1000, 100);
    lob_book_t *book = lob_create(16, 8);
    risk_state_t s;
    ASSERT_NOT_NULL(e);
    ASSERT_NOT_NULL(book);
    ASSERT_EQ(lob_add_order(book, 99, LOB_SIDE_SELL, 100, 6, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_OK);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 21), RISK_MARGIN);
    ASSERT_TRUE(lob_total_volume(book) == 0 && lob_order_count(book) == 1);
    ASSERT_EQ(risk_reserve(e, 1, LOB_SIDE_BUY, 100, 10), RISK_OK);
    ASSERT_EQ(lob_add_order(book, 1, LOB_SIDE_BUY, 100, 10, LOB_TYPE_IOC, apply_risk_fill, e),
              LOB_RESULT_OK);
    ASSERT_EQ(risk_release(e, 1), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.position == 6 && s.cash == 400 && s.open_orders == 0);
    ASSERT_TRUE(lob_total_volume(book) == 6 && lob_order_count(book) == 0);
    /* Venue rejection rolls back only its own reservation. */
    ASSERT_EQ(lob_add_order(book, 99, LOB_SIDE_SELL, 100, 1, LOB_TYPE_LIMIT, NULL, NULL),
              LOB_RESULT_OK);
    ASSERT_EQ(risk_reserve(e, 2, LOB_SIDE_BUY, 100, 1), RISK_OK);
    ASSERT_EQ(lob_add_order(book, 2, LOB_SIDE_BUY, 100, 1, LOB_TYPE_POST_ONLY, NULL, NULL),
              LOB_RESULT_POST_ONLY_CROSSED);
    ASSERT_EQ(risk_release(e, 2), RISK_OK);
    ASSERT_EQ(risk_get_state(e, &s), RISK_OK);
    ASSERT_TRUE(s.position == 6 && s.open_orders == 0);
    lob_free(book);
    risk_free(e);
}
