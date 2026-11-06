/* ============================================================================
 * simd_match.c — Vectorized SIMD Matching Engine Implementation
 *
 * Provides optimized SIMD kernels for ARM NEON, x86 AVX2/SSE4.2, and portable
 * auto-vectorizing C99 fallback.
 * ============================================================================ */

#include "simd_match.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ----------------------------------------------------------------------------
 * Architecture & SIMD Backend Detection
 * ---------------------------------------------------------------------------- */

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define LOB_HAS_NEON 1
#endif

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
#include <immintrin.h>
#define LOB_HAS_X86 1
#if (defined(__GNUC__) || defined(__clang__))
#define LOB_TARGET_AVX2 __attribute__((target("avx2")))
#define LOB_TARGET_SSE41 __attribute__((target("sse4.1")))
#define LOB_HAS_TARGET_ATTR 1
#else
#define LOB_TARGET_AVX2
#define LOB_TARGET_SSE41
#endif
#endif

lob_simd_backend_t lob_simd_get_backend(void) {
#if defined(LOB_HAS_NEON)
    return LOB_SIMD_BACKEND_NEON;
#elif defined(LOB_HAS_X86) && defined(LOB_HAS_TARGET_ATTR)
    if (__builtin_cpu_supports("avx2")) {
        return LOB_SIMD_BACKEND_AVX2;
    }
    if (__builtin_cpu_supports("sse4.1")) {
        return LOB_SIMD_BACKEND_SSE42;
    }
    return LOB_SIMD_BACKEND_PORTABLE;
#elif defined(__AVX2__)
    return LOB_SIMD_BACKEND_AVX2;
#elif defined(__SSE4_1__) || defined(__SSE4_2__)
    return LOB_SIMD_BACKEND_SSE42;
#else
    return LOB_SIMD_BACKEND_PORTABLE;
#endif
}

const char *lob_simd_backend_name(lob_simd_backend_t backend) {
    switch (backend) {
        case LOB_SIMD_BACKEND_NEON:
            return "ARM NEON";
        case LOB_SIMD_BACKEND_AVX2:
            return "AVX2 (256-bit)";
        case LOB_SIMD_BACKEND_SSE42:
            return "SSE4.2 (128-bit)";
        case LOB_SIMD_BACKEND_PORTABLE:
        default:
            return "Portable C99";
    }
}

const char *lob_simd_active_backend_name(void) {
    return lob_simd_backend_name(lob_simd_get_backend());
}

/* ----------------------------------------------------------------------------
 * Portable Fallback Implementations
 * ---------------------------------------------------------------------------- */

uint32_t lob_simd_match_batch_u32_portable(uint32_t taker_qty, const uint32_t *maker_qtys,
                                           uint32_t count, uint32_t *matched_qtys,
                                           uint32_t *remaining_maker_qtys,
                                           uint32_t *remaining_taker_qty) {
    uint32_t cur_taker = taker_qty;
    uint32_t total_matched = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t mq = maker_qtys[i];
        uint32_t fill = (cur_taker < mq) ? cur_taker : mq;
        matched_qtys[i] = fill;
        remaining_maker_qtys[i] = mq - fill;
        cur_taker -= fill;
        total_matched += fill;
    }

    if (remaining_taker_qty) {
        *remaining_taker_qty = cur_taker;
    }
    return total_matched;
}

uint32_t lob_simd_compare_prices_u64_portable(uint64_t limit_price, const uint64_t *level_prices,
                                              uint32_t count, lob_side_t side,
                                              uint32_t *cross_mask) {
    uint32_t mask = 0;
    uint32_t consecutive = 0;

    for (uint32_t i = 0; i < count; i++) {
        bool cross = (side == LOB_SIDE_BUY) ? (limit_price >= level_prices[i])
                                            : (limit_price <= level_prices[i]);
        if (cross) {
            mask |= (1U << i);
            consecutive++;
        } else {
            break; /* Levels are sorted; no further level can cross */
        }
    }

    if (cross_mask) {
        *cross_mask = mask;
    }
    return consecutive;
}

uint64_t lob_simd_accumulate_volume_u64_portable(const uint64_t *prices, const uint64_t *volumes,
                                                 uint32_t count, uint64_t limit_price,
                                                 lob_side_t side, uint64_t min_qty,
                                                 uint32_t *levels_checked) {
    uint64_t total_volume = 0;
    uint32_t i = 0;

    for (; i < count; i++) {
        bool cross =
            (side == LOB_SIDE_BUY) ? (limit_price >= prices[i]) : (limit_price <= prices[i]);
        if (!cross) {
            break;
        }
        total_volume += volumes[i];
        if (min_qty > 0 && total_volume >= min_qty) {
            i++;
            break;
        }
    }

    if (levels_checked) {
        *levels_checked = i;
    }
    return total_volume;
}

/* ----------------------------------------------------------------------------
 * ARM NEON Kernels
 * ---------------------------------------------------------------------------- */

#if defined(LOB_HAS_NEON)

static uint32_t match_batch_neon(uint32_t taker_qty, const uint32_t *maker_qtys, uint32_t count,
                                 uint32_t *matched_qtys, uint32_t *remaining_maker_qtys,
                                 uint32_t *remaining_taker_qty) {
    uint32_t i = 0;
    uint32_t cur_taker = taker_qty;
    uint32_t total_matched = 0;

    while (i + 4 <= count && cur_taker > 0) {
        uint32x4_t q = vld1q_u32(&maker_qtys[i]);
        uint32x4_t zero = vdupq_n_u32(0);

        /* Vectorized prefix sums with saturating addition */
        uint32x4_t s1 = vextq_u32(zero, q, 3);
        uint32x4_t p1 = vqaddq_u32(q, s1);

        uint32x4_t s2 = vextq_u32(zero, p1, 2);
        uint32x4_t p = vqaddq_u32(p1, s2);

        /* Shifted prefix: [0, p0, p1, p2] */
        uint32x4_t p_prev = vextq_u32(zero, p, 3);

        /* Available taker quantity for each lane */
        uint32x4_t v_taker = vdupq_n_u32(cur_taker);
        uint32x4_t avail = vqsubq_u32(v_taker, p_prev);

        /* Matched quantity per maker order: min(q, avail) */
        uint32x4_t m = vminq_u32(q, avail);

        /* Remaining maker resting quantity: q - m */
        uint32x4_t rem_maker = vsubq_u32(q, m);

        vst1q_u32(&matched_qtys[i], m);
        vst1q_u32(&remaining_maker_qtys[i], rem_maker);

        uint32_t p3 = vgetq_lane_u32(p, 3);
        uint32_t block_matched = (cur_taker < p3) ? cur_taker : p3;

        cur_taker -= block_matched;
        total_matched += block_matched;
        i += 4;
    }

    /* Scalar tail loop for remaining 1..3 orders or when cur_taker == 0 */
    while (i < count) {
        uint32_t mq = maker_qtys[i];
        uint32_t fill = (cur_taker < mq) ? cur_taker : mq;
        matched_qtys[i] = fill;
        remaining_maker_qtys[i] = mq - fill;
        cur_taker -= fill;
        total_matched += fill;
        i++;
    }

    if (remaining_taker_qty) {
        *remaining_taker_qty = cur_taker;
    }
    return total_matched;
}

static uint32_t compare_prices_neon(uint64_t limit_price, const uint64_t *level_prices,
                                    uint32_t count, lob_side_t side, uint32_t *cross_mask) {
    uint32_t i = 0;
    uint32_t mask = 0;
    uint32_t consecutive = 0;
    bool broke = false;

    uint64x2_t v_lim = vdupq_n_u64(limit_price);

    while (i + 2 <= count && !broke) {
        uint64x2_t p = vld1q_u64(&level_prices[i]);
        uint64x2_t cmp = (side == LOB_SIDE_BUY) ? vcgeq_u64(v_lim, p) : vcleq_u64(v_lim, p);

        uint64_t c0 = vgetq_lane_u64(cmp, 0);
        uint64_t c1 = vgetq_lane_u64(cmp, 1);

        if (c0) {
            mask |= (1U << i);
            consecutive++;
        } else {
            broke = true;
        }

        if (!broke && c1) {
            mask |= (1U << (i + 1));
            consecutive++;
        } else {
            broke = true;
        }
        i += 2;
    }

    if (!broke && i < count) {
        bool cross = (side == LOB_SIDE_BUY) ? (limit_price >= level_prices[i])
                                            : (limit_price <= level_prices[i]);
        if (cross) {
            mask |= (1U << i);
            consecutive++;
        }
    }

    if (cross_mask) {
        *cross_mask = mask;
    }
    return consecutive;
}

static uint64_t accumulate_volume_neon(const uint64_t *prices, const uint64_t *volumes,
                                       uint32_t count, uint64_t limit_price, lob_side_t side,
                                       uint64_t min_qty, uint32_t *levels_checked) {
    uint32_t i = 0;
    uint64_t total_volume = 0;
    uint64x2_t v_lim = vdupq_n_u64(limit_price);
    bool early_stop = false;

    while (i + 2 <= count && !early_stop) {
        uint64x2_t p = vld1q_u64(&prices[i]);
        uint64x2_t v = vld1q_u64(&volumes[i]);

        uint64x2_t cmp = (side == LOB_SIDE_BUY) ? vcgeq_u64(v_lim, p) : vcleq_u64(v_lim, p);
        uint64x2_t exec_v = vandq_u64(v, cmp);

        uint64_t c0 = vgetq_lane_u64(cmp, 0);
        uint64_t c1 = vgetq_lane_u64(cmp, 1);

        if (c0 == 0) {
            early_stop = true;
            break;
        }

        uint64_t v0 = vgetq_lane_u64(exec_v, 0);
        total_volume += v0;

        if (min_qty > 0 && total_volume >= min_qty) {
            i += 1;
            early_stop = true;
            break;
        }

        if (c1 == 0) {
            i += 1;
            early_stop = true;
            break;
        }

        uint64_t v1 = vgetq_lane_u64(exec_v, 1);
        total_volume += v1;
        i += 2;

        if (min_qty > 0 && total_volume >= min_qty) {
            early_stop = true;
            break;
        }
    }

    while (i < count && !early_stop) {
        bool cross =
            (side == LOB_SIDE_BUY) ? (limit_price >= prices[i]) : (limit_price <= prices[i]);
        if (!cross) {
            break;
        }
        total_volume += volumes[i];
        i++;
        if (min_qty > 0 && total_volume >= min_qty) {
            break;
        }
    }

    if (levels_checked) {
        *levels_checked = i;
    }
    return total_volume;
}

#endif /* LOB_HAS_NEON */

/* ----------------------------------------------------------------------------
 * x86 AVX2 / SSE4 Kernels
 * ---------------------------------------------------------------------------- */

#if defined(LOB_HAS_X86) && defined(LOB_HAS_TARGET_ATTR)

LOB_TARGET_AVX2
static uint32_t match_batch_avx2(uint32_t taker_qty, const uint32_t *maker_qtys, uint32_t count,
                                 uint32_t *matched_qtys, uint32_t *remaining_maker_qtys,
                                 uint32_t *remaining_taker_qty) {
    uint32_t i = 0;
    uint32_t cur_taker = taker_qty;
    uint32_t total_matched = 0;

    while (i + 4 <= count && cur_taker > 0) {
        __m128i q = _mm_loadu_si128((const __m128i *)(const void *)&maker_qtys[i]);

        /* Prefix sum */
        __m128i a = _mm_add_epi32(q, _mm_slli_si128(q, 4));
        __m128i p = _mm_add_epi32(a, _mm_slli_si128(a, 8));
        __m128i p_prev = _mm_slli_si128(p, 4);

        __m128i v_taker = _mm_set1_epi32((int)cur_taker);
        __m128i diff = _mm_sub_epi32(v_taker, p_prev);

        /* Branchless saturating subtract: clamp to 0 if cur_taker < p_prev */
        __m128i max_val = _mm_max_epu32(v_taker, p_prev);
        __m128i cmp = _mm_cmpeq_epi32(max_val, v_taker);
        __m128i avail = _mm_and_si128(diff, cmp);

        __m128i m = _mm_min_epu32(q, avail);
        __m128i rem_maker = _mm_sub_epi32(q, m);

        _mm_storeu_si128((__m128i *)(void *)&matched_qtys[i], m);
        _mm_storeu_si128((__m128i *)(void *)&remaining_maker_qtys[i], rem_maker);

        uint32_t p3 = (uint32_t)_mm_extract_epi32(p, 3);
        uint32_t block_matched = (cur_taker < p3) ? cur_taker : p3;

        cur_taker -= block_matched;
        total_matched += block_matched;
        i += 4;
    }

    while (i < count) {
        uint32_t mq = maker_qtys[i];
        uint32_t fill = (cur_taker < mq) ? cur_taker : mq;
        matched_qtys[i] = fill;
        remaining_maker_qtys[i] = mq - fill;
        cur_taker -= fill;
        total_matched += fill;
        i++;
    }

    if (remaining_taker_qty) {
        *remaining_taker_qty = cur_taker;
    }
    return total_matched;
}

LOB_TARGET_AVX2
static uint32_t compare_prices_avx2(uint64_t limit_price, const uint64_t *level_prices,
                                    uint32_t count, lob_side_t side, uint32_t *cross_mask) {
    uint32_t i = 0;
    uint32_t mask = 0;
    uint32_t consecutive = 0;
    bool broke = false;

    __m256i v_lim = _mm256_set1_epi64x((long long)limit_price);
    __m256i sign_flip = _mm256_set1_epi64x((long long)0x8000000000000000ULL);
    __m256i v_lim_flipped = _mm256_xor_si256(v_lim, sign_flip);

    while (i + 4 <= count && !broke) {
        __m256i p = _mm256_loadu_si256((const __m256i *)(const void *)&level_prices[i]);
        __m256i p_flipped = _mm256_xor_si256(p, sign_flip);

        __m256i cmp;
        if (side == LOB_SIDE_BUY) {
            /* limit_price >= price <=> !(price > limit_price) */
            __m256i gt = _mm256_cmpgt_epi64(p_flipped, v_lim_flipped);
            cmp = _mm256_xor_si256(gt, _mm256_set1_epi32(-1));
        } else {
            /* limit_price <= price <=> !(limit_price > price) */
            __m256i gt = _mm256_cmpgt_epi64(v_lim_flipped, p_flipped);
            cmp = _mm256_xor_si256(gt, _mm256_set1_epi32(-1));
        }

        int m = _mm256_movemask_pd(_mm256_castsi256_pd(cmp));
        for (int k = 0; k < 4; k++) {
            if ((m & (1 << k)) != 0 && !broke) {
                mask |= (1U << (i + k));
                consecutive++;
            } else {
                broke = true;
            }
        }
        i += 4;
    }

    if (!broke && i < count) {
        for (; i < count; i++) {
            bool cross = (side == LOB_SIDE_BUY) ? (limit_price >= level_prices[i])
                                                : (limit_price <= level_prices[i]);
            if (cross) {
                mask |= (1U << i);
                consecutive++;
            } else {
                break;
            }
        }
    }

    if (cross_mask) {
        *cross_mask = mask;
    }
    return consecutive;
}

#endif /* LOB_HAS_X86 */

/* ----------------------------------------------------------------------------
 * Public Dispatch Functions
 * ---------------------------------------------------------------------------- */

uint32_t lob_simd_match_batch_u32(uint32_t taker_qty, const uint32_t *maker_qtys, uint32_t count,
                                  uint32_t *matched_qtys, uint32_t *remaining_maker_qtys,
                                  uint32_t *remaining_taker_qty) {
#if defined(LOB_HAS_NEON)
    return match_batch_neon(taker_qty, maker_qtys, count, matched_qtys, remaining_maker_qtys,
                            remaining_taker_qty);
#elif defined(LOB_HAS_X86) && defined(LOB_HAS_TARGET_ATTR)
    if (__builtin_cpu_supports("avx2")) {
        return match_batch_avx2(taker_qty, maker_qtys, count, matched_qtys, remaining_maker_qtys,
                                remaining_taker_qty);
    }
    return lob_simd_match_batch_u32_portable(taker_qty, maker_qtys, count, matched_qtys,
                                             remaining_maker_qtys, remaining_taker_qty);
#else
    return lob_simd_match_batch_u32_portable(taker_qty, maker_qtys, count, matched_qtys,
                                             remaining_maker_qtys, remaining_taker_qty);
#endif
}

uint32_t lob_simd_compare_prices_u64(uint64_t limit_price, const uint64_t *level_prices,
                                     uint32_t count, lob_side_t side, uint32_t *cross_mask) {
#if defined(LOB_HAS_NEON)
    return compare_prices_neon(limit_price, level_prices, count, side, cross_mask);
#elif defined(LOB_HAS_X86) && defined(LOB_HAS_TARGET_ATTR)
    if (__builtin_cpu_supports("avx2")) {
        return compare_prices_avx2(limit_price, level_prices, count, side, cross_mask);
    }
    return lob_simd_compare_prices_u64_portable(limit_price, level_prices, count, side, cross_mask);
#else
    return lob_simd_compare_prices_u64_portable(limit_price, level_prices, count, side, cross_mask);
#endif
}

uint64_t lob_simd_accumulate_volume_u64(const uint64_t *prices, const uint64_t *volumes,
                                        uint32_t count, uint64_t limit_price, lob_side_t side,
                                        uint64_t min_qty, uint32_t *levels_checked) {
#if defined(LOB_HAS_NEON)
    return accumulate_volume_neon(prices, volumes, count, limit_price, side, min_qty,
                                  levels_checked);
#else
    return lob_simd_accumulate_volume_u64_portable(prices, volumes, count, limit_price, side,
                                                   min_qty, levels_checked);
#endif
}
