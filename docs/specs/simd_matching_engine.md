# SIMD Matching Engine Specification

## 1. Overview

The QForge SIMD Matching Engine provides vectorized instruction optimizations for order matching in the Level 3 (Market-By-Order) limit order book. By exploiting hardware vector registers (ARM NEON 128-bit, Intel/AMD AVX2 256-bit, SSE4.2 128-bit, and portable C99 auto-vectorized loops), the engine executes matching logic with sub-microsecond latency and near-gigahertz order evaluation rates.

Zero third-party library dependencies are required. All vector operations adhere strictly to C99 standards with compiler target multi-versioning.

---

## 2. Architectural Design & Vector Operations

### 2.1 Vectorized Batch Quantity Matching (`lob_simd_match_batch_u32`)

When aggressive taker volume crosses resting liquidity within a price level or sweeps a consolidated order queue, scalar matching typically incurs branch mispredictions and iterative dependency chains.

The SIMD Matching Engine vectorizes up to 4 orders (128-bit) or 8 orders (256-bit) per cycle:

1. **Vectorized Saturating Prefix Sum**:
   Given resting order quantities $\mathbf{q} = [q_0, q_1, q_2, q_3]$:
   $$\mathbf{p}_1 = \mathbf{q} \oplus_{\text{sat}} (\mathbf{q} \gg 1)$$
   $$\mathbf{p} = \mathbf{p}_1 \oplus_{\text{sat}} (\mathbf{p}_1 \gg 2) = [q_0, q_0 + q_1, q_0 + q_1 + q_2, \sum_{i=0}^3 q_i]$$
   Saturating arithmetic (`vqaddq_u32` on NEON) eliminates any integer wraparound hazard if order sizes approach $2^{32}-1$.

2. **Shifted Available Budget Calculation**:
   Extract prior cumulative quantity $\mathbf{p}_{\text{prev}} = [0, p_0, p_1, p_2]$.
   Compute available taker budget via unsigned saturating subtraction:
   $$\mathbf{a} = \mathbf{q}_{\text{taker}} \ominus_{\text{sat}} \mathbf{p}_{\text{prev}}$$
   If taker quantity is exhausted by earlier orders, subsequent lanes saturate cleanly to 0 without branch instructions.

3. **Matched Fill Vector & Residual Quantities**:
   $$\mathbf{m} = \min(\mathbf{q}, \mathbf{a})$$
   $$\mathbf{q}_{\text{rem}} = \mathbf{q} - \mathbf{m}$$
   Filled orders ($\mathbf{q}_{\text{rem}} == 0$) are unlinked and recycled in batch back to the lock-free Treiber pool.

### 2.2 Vectorized Price Crossing & Level Filtering (`lob_simd_compare_prices_u64`)

In aggressive multi-level sweeps, crossing price verification compares 64-bit tick prices against the taker limit price in parallel:
- **Buy Taker**: $P_{\text{limit}} \ge P_{\text{ask}}$
- **Sell Taker**: $P_{\text{limit}} \le P_{\text{bid}}$

SIMD 64-bit vector comparisons (`vcgeq_u64` / `vcleq_u64` on ARM NEON, sign-flipped `_mm256_cmpgt_epi64` on AVX2) generate a bitmask of eligible price levels in a single instruction cycle, bypassing branch prediction overhead.

### 2.3 Vectorized Volume & Depth Sweep (`lob_simd_accumulate_volume_u64`)

For **Fill-Or-Kill (FOK)** orders and pre-trade margin checks:
- Evaluates book depth and resting volumes across price levels simultaneously.
- Masks volumes against the crossing price comparison mask (`vandq_u64`).
- Horizontal vector accumulation terminates early as soon as the required threshold quantity is satisfied.

---

## 3. Supported Hardware Backends

| Backend | Vector Width | Architecture | Intrinsics Header |
| :--- | :--- | :--- | :--- |
| **ARM NEON** | 128-bit (4×32b, 2×64b) | AArch64 / Apple Silicon / AWS Graviton | `<arm_neon.h>` |
| **AVX2** | 256-bit (8×32b, 4×64b) | x86-64 Haswell+ / Zen+ | `<immintrin.h>` |
| **SSE4.2** | 128-bit (4×32b, 2×64b) | x86-64 Core i7+ | `<immintrin.h>` |
| **Portable C99** | Scalar / Auto-vectorized | Any platform (WebAssembly, RISC-V, etc.) | Standard C99 |

The engine detects CPU features at compile time and runtime via `lob_simd_get_backend()`.

---

## 4. Benchmark Results

Measured on 64-bit Linux (`aarch64`, GCC `-O2`):

| Operation | Throughput | Latency |
| :--- | :--- | :--- |
| **Raw SIMD Batch Matching (4-order block)** | ~988.2M orders/sec | **~1.01 ns / order** (4.0 ns / 4-order batch) |
| **Full Trade Matching (Crossing Path with Book)** | ~4.1M to 6.0M matches/sec | **~160 - 240 ns / match** |
| **Lock-Free Command Ring Buffer** | ~73.0M ops/sec | **~13.7 ns / op** |
| **O(1) Order Cancellations** | ~6.7M cancels/sec | **~148.8 ns / cancel** |

---

## 5. Verification & Sanitizers

The SIMD Matching Engine is validated with:
- Permutation equivalence testing between hardware SIMD and the portable C99 reference implementation.
- AddressSanitizer (ASan) memory leak check (`make memcheck`): zero memory leaks.
- UndefinedBehaviorSanitizer (UBSan): zero undefined behavior warnings.
- C++17 RAII wrapper integration test suite (`make test_cpp`).
- Black-box end-to-end integration test runner (`make integration`).
