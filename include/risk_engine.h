/* Deterministic pre-trade controls for one account and one instrument. */
#ifndef RISK_ENGINE_H
#define RISK_ENGINE_H

#include "order_book.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RISK_OK = 0,
    RISK_INVALID_ARG = -1,
    RISK_DUPLICATE_ORDER = -2,
    RISK_ORDER_NOT_FOUND = -3,
    RISK_CAPACITY = -4,
    RISK_HALTED = -5,
    RISK_ORDER_QTY = -6,
    RISK_ORDER_NOTIONAL = -7,
    RISK_POSITION_LIMIT = -8,
    RISK_MARGIN = -9,
    RISK_OVERFLOW = -10
} risk_result_t;

typedef struct {
    uint32_t max_order_qty;
    uint64_t max_order_notional;
    uint64_t max_abs_position;
    uint32_t initial_margin_bps;
    uint32_t maintenance_margin_bps;
} risk_limits_t;

typedef struct {
    int64_t cash;
    int64_t position;
    int64_t equity;
    uint64_t mark_price;
    uint64_t reserved_notional;
    uint64_t reserved_loss;
    uint64_t initial_margin;
    uint64_t maintenance_margin;
    size_t open_orders;
    bool maintenance_breached;
    bool halted;
} risk_state_t;

typedef struct risk_engine risk_engine_t;

/* Positive integer prices and money share the book's tick units; multiplier 1.
 * All monetary magnitudes and positions must fit INT64_MAX. Rates are 1..10000,
 * maintenance <= initial. Capacity bounds open reservations. Single writer;
 * serialize all account events and order routing on the book consumer thread. */
risk_engine_t *risk_create(const risk_limits_t *limits, size_t capacity, int64_t cash,
                           uint64_t mark_price);
void risk_free(risk_engine_t *engine);

/* Check AND reserve atomically before routing an order. Buy price is a maximum
 * execution price, sell price a minimum. Market orders require a venue-enforced
 * price bound (route as an IOC limit). Rejections leave reservations unchanged. */
risk_result_t risk_reserve(risk_engine_t *engine, uint64_t order_id, lob_side_t side,
                           uint64_t limit_price, uint32_t qty);

/* Apply each confirmed execution exactly once, including partial fills. Updates
 * cash/position and releases only the filled reservation. Works while halted or
 * in margin breach. Invalid reports leave state unchanged; callers must halt and
 * reconcile any execution report that cannot be applied. */
risk_result_t risk_fill(risk_engine_t *engine, uint64_t order_id, uint64_t execution_price,
                        uint32_t qty);

/* Release the remainder ONLY after venue cancellation/rejection confirmation,
 * or when a synchronous submit fails. Also use for unfilled IOC/FOK remainder. */
risk_result_t risk_release(risk_engine_t *engine, uint64_t order_id);

/* A valid mark is stored even if valuation overflows: queries and pre-trade
 * checks then return RISK_OVERFLOW until a representable mark arrives. */
risk_result_t risk_mark(risk_engine_t *engine, uint64_t mark_price);
/* Output is unchanged on error. Serialize queries with account events. */
risk_result_t risk_get_state(const risk_engine_t *engine, risk_state_t *state);
/* Stops new reservations; never discards live reservations or fills. */
risk_result_t risk_set_halted(risk_engine_t *engine, bool halted);

#ifdef __cplusplus
}
#endif
#endif /* RISK_ENGINE_H */
