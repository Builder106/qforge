/* ============================================================================
 * simd_match.h — Vectorized SIMD Matching Engine for L3 Limit Order Book
 *
 * Implements high-throughput, sub-microsecond vectorized matching operations
 * using SIMD instruction sets (ARM NEON, x86 AVX2/SSE4.2, and portable fallback):
 *
 * 1. Vectorized Batch Quantity Matching: Computes matched execution quantities,
 *    updated resting quantities, and fill statuses across 4-8 orders simultaneously
 *    using SIMD prefix sums and branchless saturating clamp arithmetic.
 *
 * 2. Vectorized Price Crossing Detection: Compares 64-bit tick prices in parallel
 *    to find crossing levels and execution masks in a single instruction cycle.
 *
 * 3. Vectorized Depth & Volume Sweep: Fast parallel volume accumulation for
 *    Fill-Or-Kill (FOK) pre-trade verification and aggressive market order sweeps.
 *
 * 4. Cache-Aligned Level Matching: Directly drives FIFO price-time execution
 *    within L3 order book price levels with zero memory allocations.
 * ============================================================================ */

#ifndef SIMD_MATCH_H
#define SIMD_MATCH_H

#include "order_book.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * SIMD Instruction Backend Enumeration
 * ---------------------------------------------------------------------------- */

typedef enum {
    LOB_SIMD_BACKEND_PORTABLE = 0, /* Pure C99 auto-vectorized loop fallback */
    LOB_SIMD_BACKEND_SSE42 = 1,    /* Intel/AMD SSE4.2 / SSE2 (128-bit) */
    LOB_SIMD_BACKEND_AVX2 = 2,     /* Intel/AMD AVX2 (256-bit) */
    LOB_SIMD_BACKEND_NEON = 3      /* ARM NEON / Advanced SIMD (128-bit) */
} lob_simd_backend_t;

/*
 * Detect and return the optimal SIMD backend active at compile time / runtime.
 */
lob_simd_backend_t lob_simd_get_backend(void);

/*
 * Returns a human-readable display string for the specified backend.
 */
const char *lob_simd_backend_name(lob_simd_backend_t backend);

/*
 * Returns the name of the currently active SIMD backend.
 */
const char *lob_simd_active_backend_name(void);

/* ----------------------------------------------------------------------------
 * Vectorized Quantity Matching Primitives
 * ---------------------------------------------------------------------------- */

/*
 * Vectorized batch quantity matching for up to `count` resting orders.
 *
 * Given a taker incoming quantity `taker_qty` and an array of resting maker
 * quantities `maker_qtys` (of length `count`):
 * - Computes matched quantities in `matched_qtys` using SIMD prefix sums
 *   and saturating clamp arithmetic.
 * - Computes remaining resting quantities in `remaining_maker_qtys`.
 * - Sets `remaining_taker_qty` (if non-NULL) to the unfilled portion of taker_qty.
 * - Returns the total matched volume across the batch.
 *
 * Branchless, overflow-safe, and vectorized across 4-lane (128-bit) or
 * 8-lane (256-bit) SIMD execution units.
 */
uint32_t lob_simd_match_batch_u32(uint32_t taker_qty, const uint32_t *maker_qtys, uint32_t count,
                                  uint32_t *matched_qtys, uint32_t *remaining_maker_qtys,
                                  uint32_t *remaining_taker_qty);

/*
 * Portable fallback implementation for vectorized quantity matching (useful
 * for verification against hardware SIMD routines and architectures without SIMD).
 */
uint32_t lob_simd_match_batch_u32_portable(uint32_t taker_qty, const uint32_t *maker_qtys,
                                           uint32_t count, uint32_t *matched_qtys,
                                           uint32_t *remaining_maker_qtys,
                                           uint32_t *remaining_taker_qty);

/* ----------------------------------------------------------------------------
 * Vectorized Price Crossing & Level Filtering
 * ---------------------------------------------------------------------------- */

/*
 * Compares an array of 64-bit level prices against `limit_price` in parallel.
 *
 * For LOB_SIDE_BUY:  levels cross if limit_price >= level_price.
 * For LOB_SIDE_SELL: levels cross if limit_price <= level_price.
 *
 * Sets bit i in `cross_mask` (if non-NULL) to 1 if level_prices[i] crosses.
 * Returns the number of consecutive crossing levels starting from index 0.
 */
uint32_t lob_simd_compare_prices_u64(uint64_t limit_price, const uint64_t *level_prices,
                                     uint32_t count, lob_side_t side, uint32_t *cross_mask);

/*
 * Portable fallback implementation for price crossing comparison.
 */
uint32_t lob_simd_compare_prices_u64_portable(uint64_t limit_price, const uint64_t *level_prices,
                                              uint32_t count, lob_side_t side,
                                              uint32_t *cross_mask);

/* ----------------------------------------------------------------------------
 * Vectorized Depth & Volume Accumulation
 * ---------------------------------------------------------------------------- */

/*
 * Vectorized accumulation of resting volumes for price levels that cross `limit_price`.
 *
 * Evaluates up to `count` price levels in parallel SIMD vectors.
 * Accumulates volume until all crossing levels are processed or until
 * accumulated volume reaches `min_qty` (if min_qty > 0).
 *
 * If `levels_checked` is non-NULL, writes the number of levels examined.
 * Returns the total executable volume available.
 */
uint64_t lob_simd_accumulate_volume_u64(const uint64_t *prices, const uint64_t *volumes,
                                        uint32_t count, uint64_t limit_price, lob_side_t side,
                                        uint64_t min_qty, uint32_t *levels_checked);

/*
 * Portable fallback implementation for volume accumulation.
 */
uint64_t lob_simd_accumulate_volume_u64_portable(const uint64_t *prices, const uint64_t *volumes,
                                                 uint32_t count, uint64_t limit_price,
                                                 lob_side_t side, uint64_t min_qty,
                                                 uint32_t *levels_checked);

/* ----------------------------------------------------------------------------
 * High-Level SIMD Matching Engine Operations
 * ---------------------------------------------------------------------------- */

/*
 * Matches an incoming taker order against resting orders at `level` using
 * vectorized SIMD batch execution.
 *
 * Updates maker orders, unlinks fully filled orders, recycles slots to
 * book->order_pool, and invokes `on_match` callback.
 * Decrements `*remaining_qty` by the matched amount.
 * Returns the total matched quantity at this level.
 */
uint32_t lob_simd_match_level(lob_book_t *book, lob_price_level_t *level, uint64_t taker_order_id,
                              lob_side_t taker_side, uint32_t *remaining_qty,
                              lob_match_callback_t on_match, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* SIMD_MATCH_H */
