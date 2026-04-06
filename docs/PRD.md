# C-Neural-Engine — Product Requirements Document

## Vision
A **zero-dependency deep learning framework written entirely in C99**, implementing core neural network primitives — matrix algebra, automatic differentiation (backpropagation), activation functions, loss functions, and gradient-based optimizers — from scratch.

## Goals
1. **Zero external dependencies** — no BLAS, no libm beyond `<math.h>`, no third-party libraries
2. **Clean, modular C99** — compiles with `-std=c99 -Wall -Wextra -Werror -pedantic`
3. **Correct numerics** — validated via TDD at every layer of the stack
4. **Memory safety** — no leaks, verified with Valgrind / LeakSanitizer
5. **Train a real model** — solve XOR and MNIST-subset as proof of correctness

## Architecture

6 modules, tested independently:

| # | Module | Purpose |
|---|--------|---------|
| 1 | **Tensor** | Matrix struct, allocation, arithmetic, transpose |
| 2 | **Activation** | ReLU, Sigmoid, Tanh, Softmax + derivatives |
| 3 | **Loss** | MSE, Cross-Entropy + derivatives |
| 4 | **Layer** | Dense layer: forward/backward, weight init |
| 5 | **Network** | Sequential model: stack layers, train, predict |
| 6 | **Optimizer** | SGD, SGD+Momentum |

## Technical Decisions
- **Data type**: `double` (64-bit) for numerical precision
- **Test framework**: Custom zero-dependency test harness (~120 lines)
- **Weight init**: Xavier (sigmoid/tanh) and He (ReLU), auto-selected
- **Error handling**: `assert()` for fail-fast on dimension mismatches
- **Numerical stability**: Sigmoid (sign-branched), Softmax (subtract-max), Cross-entropy (epsilon-clamped)

## Verification
- 51 unit tests across all modules
- AddressSanitizer / Valgrind for memory safety
- XOR convergence to < 0.01 MSE as functional validation
