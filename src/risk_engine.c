#include "risk_engine.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t id;
    uint64_t price;
    uint32_t qty;
    lob_side_t side;
} risk_order_t;

struct risk_engine {
    risk_limits_t limits;
    risk_order_t *orders;
    size_t capacity;
    size_t count;
    int64_t cash;
    int64_t position;
    uint64_t mark;
    bool halted;
};

static bool valid_price(uint64_t price) {
    return price > 0 && price <= INT64_MAX;
}

/* Monetary aggregates deliberately use a signed-64-bit magnitude ceiling so
 * converting between cash, exposure and equity never wraps or truncates. */
static bool add_amount(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > (uint64_t)INT64_MAX - a)
        return false;
    *out = a + b;
    return true;
}

static bool multiply_amount(uint64_t a, uint64_t b, uint64_t *out) {
    if (b && a > (uint64_t)INT64_MAX / b)
        return false;
    *out = a * b;
    return true;
}

static bool add_signed(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return false;
    *out = a + b;
    return true;
}

static uint64_t margin(uint64_t notional, uint32_t bps) {
    /* Quotient/remainder avoids overflowing notional * bps; round up. */
    return (notional / 10000) * bps + ((notional % 10000) * bps + 9999) / 10000;
}

static risk_order_t *find_order(risk_engine_t *engine, uint64_t id) {
    size_t i;
    for (i = 0; i < engine->capacity; ++i)
        if (engine->orders[i].id == id)
            return &engine->orders[i];
    return NULL;
}

static risk_result_t value_state(const risk_engine_t *engine, const risk_order_t *extra,
                                 risk_state_t *out, uint64_t *buys, uint64_t *sells) {
    risk_state_t state;
    uint64_t position_value, gross, buy_qty = 0, sell_qty = 0;
    uint64_t magnitude = (uint64_t)(engine->position < 0 ? -engine->position : engine->position);
    size_t i;
    memset(&state, 0, sizeof(state));
    if (!multiply_amount(magnitude, engine->mark, &position_value))
        return RISK_OVERFLOW;
    if (!add_signed(engine->cash,
                    engine->position < 0 ? -(int64_t)position_value : (int64_t)position_value,
                    &state.equity))
        return RISK_OVERFLOW;
    /* One extra virtual slot lets pre-trade checks include the candidate without
     * exposing a partial reservation or modifying the live engine. */
    for (i = 0; i <= engine->capacity; ++i) {
        const risk_order_t *order = i == engine->capacity ? extra : &engine->orders[i];
        uint64_t notional, price, loss_per_unit = 0, loss;
        if (!order || !order->id)
            continue;
        price = order->price > engine->mark ? order->price : engine->mark;
        if (!multiply_amount(price, order->qty, &notional) ||
            !add_amount(state.reserved_notional, notional, &state.reserved_notional))
            return RISK_OVERFLOW;
        if (order->side == LOB_SIDE_BUY && order->price > engine->mark)
            loss_per_unit = order->price - engine->mark;
        if (order->side == LOB_SIDE_SELL && order->price < engine->mark)
            loss_per_unit = engine->mark - order->price;
        if (!multiply_amount(loss_per_unit, order->qty, &loss) ||
            !add_amount(state.reserved_loss, loss, &state.reserved_loss))
            return RISK_OVERFLOW;
        if (order->side == LOB_SIDE_BUY) {
            if (!add_amount(buy_qty, order->qty, &buy_qty))
                return RISK_OVERFLOW;
        } else if (!add_amount(sell_qty, order->qty, &sell_qty)) {
            return RISK_OVERFLOW;
        }
    }
    if (!add_amount(position_value, state.reserved_notional, &gross))
        return RISK_OVERFLOW;
    state.cash = engine->cash;
    state.position = engine->position;
    state.mark_price = engine->mark;
    state.initial_margin = margin(gross, engine->limits.initial_margin_bps);
    state.maintenance_margin = margin(position_value, engine->limits.maintenance_margin_bps);
    state.maintenance_breached =
        state.equity < 0 || (uint64_t)state.equity < state.maintenance_margin;
    state.open_orders = engine->count + (extra ? 1 : 0);
    state.halted = engine->halted;
    *out = state;
    if (buys)
        *buys = buy_qty;
    if (sells)
        *sells = sell_qty;
    return RISK_OK;
}

risk_engine_t *risk_create(const risk_limits_t *limits, size_t capacity, int64_t cash,
                           uint64_t mark_price) {
    risk_engine_t *engine;
    if (!limits || !capacity || capacity > SIZE_MAX / sizeof(risk_order_t) || cash < 0 ||
        !valid_price(mark_price) || !limits->max_order_qty || !limits->max_order_notional ||
        limits->max_order_notional > INT64_MAX || !limits->max_abs_position ||
        limits->max_abs_position > INT64_MAX || !limits->maintenance_margin_bps ||
        limits->maintenance_margin_bps > limits->initial_margin_bps ||
        limits->initial_margin_bps > 10000)
        return NULL;
    engine = (risk_engine_t *)calloc(1, sizeof(*engine));
    if (!engine)
        return NULL;
    engine->orders = (risk_order_t *)calloc(capacity, sizeof(*engine->orders));
    if (!engine->orders) {
        free(engine);
        return NULL;
    }
    engine->limits = *limits;
    engine->capacity = capacity;
    engine->cash = cash;
    engine->mark = mark_price;
    return engine;
}

void risk_free(risk_engine_t *engine) {
    if (!engine)
        return;
    free(engine->orders);
    free(engine);
}

risk_result_t risk_reserve(risk_engine_t *engine, uint64_t order_id, lob_side_t side,
                           uint64_t limit_price, uint32_t qty) {
    risk_order_t order;
    risk_state_t state;
    risk_result_t result;
    uint64_t notional, buys, sells, required;
    int64_t long_position, short_position;
    size_t i;
    if (!engine || !order_id || (side != LOB_SIDE_BUY && side != LOB_SIDE_SELL) ||
        !valid_price(limit_price) || !qty)
        return RISK_INVALID_ARG;
    if (engine->halted)
        return RISK_HALTED;
    if (find_order(engine, order_id))
        return RISK_DUPLICATE_ORDER;
    if (engine->count == engine->capacity)
        return RISK_CAPACITY;
    if (qty > engine->limits.max_order_qty)
        return RISK_ORDER_QTY;
    if (!multiply_amount(limit_price > engine->mark ? limit_price : engine->mark, qty, &notional))
        return RISK_OVERFLOW;
    if (notional > engine->limits.max_order_notional)
        return RISK_ORDER_NOTIONAL;
    order.id = order_id;
    order.price = limit_price;
    order.qty = qty;
    order.side = side;
    result = value_state(engine, &order, &state, &buys, &sells);
    if (result != RISK_OK)
        return result;
    /* Opposite-side orders may never execute: test both one-sided extremes. */
    if (!add_signed(engine->position, (int64_t)buys, &long_position) ||
        !add_signed(engine->position, -(int64_t)sells, &short_position) ||
        long_position > (int64_t)engine->limits.max_abs_position ||
        short_position < -(int64_t)engine->limits.max_abs_position)
        return RISK_POSITION_LIMIT;
    if (!add_amount(state.initial_margin, state.reserved_loss, &required))
        return RISK_OVERFLOW;
    if (state.maintenance_breached || state.equity < 0 || (uint64_t)state.equity < required)
        return RISK_MARGIN;
    for (i = 0; i < engine->capacity; ++i) {
        if (!engine->orders[i].id) {
            engine->orders[i] = order;
            engine->count++;
            return RISK_OK;
        }
    }
    return RISK_CAPACITY;
}

risk_result_t risk_fill(risk_engine_t *engine, uint64_t order_id, uint64_t execution_price,
                        uint32_t qty) {
    risk_order_t *order;
    uint64_t notional;
    int64_t cash, position;
    if (!engine || !order_id || !valid_price(execution_price) || !qty)
        return RISK_INVALID_ARG;
    order = find_order(engine, order_id);
    if (!order)
        return RISK_ORDER_NOT_FOUND;
    if (qty > order->qty || (order->side == LOB_SIDE_BUY && execution_price > order->price) ||
        (order->side == LOB_SIDE_SELL && execution_price < order->price))
        return RISK_INVALID_ARG;
    if (!multiply_amount(execution_price, qty, &notional) ||
        !add_signed(engine->cash,
                    order->side == LOB_SIDE_BUY ? -(int64_t)notional : (int64_t)notional, &cash) ||
        !add_signed(engine->position, order->side == LOB_SIDE_BUY ? (int64_t)qty : -(int64_t)qty,
                    &position) ||
        position == INT64_MIN)
        return RISK_OVERFLOW;
    engine->cash = cash;
    engine->position = position;
    order->qty -= qty;
    if (!order->qty) {
        order->id = 0;
        engine->count--;
    }
    return RISK_OK;
}

risk_result_t risk_release(risk_engine_t *engine, uint64_t order_id) {
    risk_order_t *order;
    if (!engine || !order_id)
        return RISK_INVALID_ARG;
    order = find_order(engine, order_id);
    if (!order)
        return RISK_ORDER_NOT_FOUND;
    order->id = 0;
    engine->count--;
    return RISK_OK;
}

risk_result_t risk_mark(risk_engine_t *engine, uint64_t mark_price) {
    risk_state_t state;
    if (!engine || !valid_price(mark_price))
        return RISK_INVALID_ARG;
    engine->mark = mark_price;
    return value_state(engine, NULL, &state, NULL, NULL);
}

risk_result_t risk_get_state(const risk_engine_t *engine, risk_state_t *state) {
    if (!engine || !state)
        return RISK_INVALID_ARG;
    return value_state(engine, NULL, state, NULL, NULL);
}

risk_result_t risk_set_halted(risk_engine_t *engine, bool halted) {
    if (!engine)
        return RISK_INVALID_ARG;
    engine->halted = halted;
    return RISK_OK;
}
