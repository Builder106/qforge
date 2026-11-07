# Risk Limit Engine

## Scope and prerequisites

The backend risk engine (`include/risk_engine.h`, `src/risk_engine.c`) manages one
account trading one instrument, using the existing L3 book's side types and
integer tick prices. Create one engine per account/instrument. Accounts start
flat with nonnegative cash. Multi-instrument portfolio netting, exchange-specific
margin schedules, fees, funding, liquidation, persistence and UI are outside this
milestone. This is a configurable simulator margin model, not an exchange margin
calculator.

All events must be serialized on the order book consumer thread. There are no
allocations after creation. Storage is fixed at the configured maximum number
of open reservations; lookup and valuation take O(capacity) bounded scans. The
module builds as strict C99 and C++17 without additional dependencies.

## Pre-trade acceptance

`risk_reserve` checks and reserves in one operation, before the caller routes an
order. It rejects invalid inputs, duplicate live IDs, exhausted capacity, halted
accounts, excessive order quantity/notional, projected position limit violations,
and insufficient margin. Rejection leaves account and reservation state intact.
Order notional uses `max(limit_price, mark_price) * quantity`. Position limits
test both `position + all_pending_buys` and `position - all_pending_sells`:
opposite-side orders cannot hide exposure by offsetting each other.

Prices, cash and notionals share integer tick units with contract multiplier 1.
Positive prices and monetary magnitudes are bounded by `INT64_MAX`. Cash and
equity are signed; short positions and negative cash/equity are supported.
Margin rates are integer basis points, with
`1 <= maintenance_margin_bps <= initial_margin_bps <= 10000`.
Requirements round up to the next tick using overflow-safe quotient/remainder
arithmetic. Checked additions and multiplications return `RISK_OVERFLOW` rather
than wrapping.

For current position `p`, mark `m`, and open orders with limit `l` and remaining
quantity `q`:

```text
equity              = cash + p * m
position_notional   = abs(p) * m
reserved_notional   = sum(max(l, m) * q)
reserved_loss       = sum(max(l - m, 0) * q for buys)
                    + sum(max(m - l, 0) * q for sells)
initial_margin      = ceil((position_notional + reserved_notional) * initial_bps / 10000)
maintenance_margin  = ceil(position_notional * maintenance_bps / 10000)
```

New reservations require equity at least `initial_margin + reserved_loss`, as
well as no maintenance breach. `reserved_loss` covers adverse execution versus
the mark: accepting an off-market limit must not allow its immediate fill to
consume collateral that was already reserved for margin. The model deliberately
reserves gross pending exposure, including closing orders. It can reject closing
orders in a margin breach; liquidation needs a separately authorized workflow.

## Order and market event contract

1. Call `risk_reserve` and route only on `RISK_OK`. A buy limit is a maximum
   execution price and a sell limit a minimum. Market orders require a
   venue-enforced price bound; with this book, use an IOC limit order.
2. Deliver every confirmed fill exactly once through `risk_fill`, for both maker
   and taker accounts when applicable. It updates signed cash and position,
   removes only filled quantity, and drops the reservation on a complete fill.
   In a shared book, the gateway owns the order-to-account mapping.
3. Call `risk_release` only after cancellation/rejection confirmation, or for an
   unfilled IOC/FOK/market remainder once submission has completed. A cancel
   request alone must not free collateral. A synchronous submission failure
   releases its reservation after any reported fills have been applied.
4. Amend orders by confirmed cancellation followed by a fresh reservation and
   submission. Do not use direct book modification to bypass pre-trade checks.
5. Deliver each new price mark to `risk_mark`; `risk_get_state` reports equity,
   required margin, reservations and maintenance breach immediately. A valid
   mark is retained even if its valuation overflows, causing queries and new
   reservations to fail closed until valuation becomes representable again.
6. `risk_set_halted(true)` stops new reservations while existing fills, releases
   and marks continue. It does not cancel venue orders. A margin breach also
   stops new reservations, while confirmed executions must still settle.

Invalid, duplicate completed, overfilled or out-of-bound execution reports return
an error without mutation. The caller must halt routing and reconcile any report
that cannot be applied, including an unrepresentable cash update. The engine
does not deduplicate partial-fill execution IDs: event delivery and replay must
provide exactly-once execution processing. Live order IDs must be unique; reuse
only after all events for the previous order have been drained.

The raw L3 book and PCAP historical feed replay remain account-agnostic. They do
not implicitly enable risk checks: trading gateways must use the reservation
contract above. Historical exchange orders need not have simulator accounts.

## Acceptance and verification

`make test` covers input validation, quantity/notional/position limits on both
sides, outstanding reservations, exact collateral boundaries, upward rounding,
adverse execution losses, partial/full fills, cancellation/rejection release,
long/short mark-to-market, maintenance breaches, kill switches and integer
overflow. Book integration verifies that a rejected order never reaches the
matcher, IOC partial fills settle, and venue rejection releases collateral.
`make test_cpp` exercises the public API from C++17. Standard integration,
formatting, compiler and sanitizer checks apply to this module.
