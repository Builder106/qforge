/* ============================================================================
 * benchmark.c — Performance benchmarks for core tensor operations
 *
 * Measures wall-clock time for:
 *   1. Matrix multiplication (NxN)
 *   2. Matrix transpose
 *   3. Element-wise operations (add, hadamard)
 *   4. Full forward + backward pass through a network
 *
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "tensor.h"
#include "network.h"
#include "optimizer.h"
#include "loss.h"

/* ---- High-resolution timer ---- */

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* ---- Benchmark: Matrix Multiplication ---- */

static void bench_matmul(int n, int trials) {
    Tensor *a = tensor_create(n, n);
    Tensor *b = tensor_create(n, n);
    tensor_rand(a, -1.0, 1.0);
    tensor_rand(b, -1.0, 1.0);

    double total_ms = 0.0;
    for (int t = 0; t < trials; t++) {
        double start = get_time_ms();
        Tensor *c = tensor_matmul(a, b);
        double end = get_time_ms();
        total_ms += (end - start);
        tensor_free(c);
    }

    double avg_ms = total_ms / trials;
    /* FLOPs for NxN matmul: 2*N^3 (multiply + add) */
    double flops = 2.0 * (double)n * (double)n * (double)n;
    double mflops = (flops / (avg_ms / 1000.0)) / 1e6;

    printf("  matmul  %4d×%-4d │ %8.3f ms │ %8.1f MFLOP/s │ %d trials\n",
           n, n, avg_ms, mflops, trials);

    tensor_free(a);
    tensor_free(b);
}

/* ---- Benchmark: Transpose ---- */

static void bench_transpose(int n, int trials) {
    Tensor *a = tensor_create(n, n);
    tensor_rand(a, -1.0, 1.0);

    double total_ms = 0.0;
    for (int t = 0; t < trials; t++) {
        double start = get_time_ms();
        Tensor *at = tensor_transpose(a);
        double end = get_time_ms();
        total_ms += (end - start);
        tensor_free(at);
    }

    printf("  transp  %4d×%-4d │ %8.3f ms │                  │ %d trials\n",
           n, n, total_ms / trials, trials);

    tensor_free(a);
}

/* ---- Benchmark: Element-wise Add ---- */

static void bench_add(int n, int trials) {
    Tensor *a = tensor_create(n, n);
    Tensor *b = tensor_create(n, n);
    tensor_rand(a, -1.0, 1.0);
    tensor_rand(b, -1.0, 1.0);

    double total_ms = 0.0;
    for (int t = 0; t < trials; t++) {
        double start = get_time_ms();
        Tensor *c = tensor_add(a, b);
        double end = get_time_ms();
        total_ms += (end - start);
        tensor_free(c);
    }

    printf("  add     %4d×%-4d │ %8.3f ms │                  │ %d trials\n",
           n, n, total_ms / trials, trials);

    tensor_free(a);
    tensor_free(b);
}

/* ---- Benchmark: Hadamard Product ---- */

static void bench_hadamard(int n, int trials) {
    Tensor *a = tensor_create(n, n);
    Tensor *b = tensor_create(n, n);
    tensor_rand(a, -1.0, 1.0);
    tensor_rand(b, -1.0, 1.0);

    double total_ms = 0.0;
    for (int t = 0; t < trials; t++) {
        double start = get_time_ms();
        Tensor *c = tensor_hadamard(a, b);
        double end = get_time_ms();
        total_ms += (end - start);
        tensor_free(c);
    }

    printf("  hadam   %4d×%-4d │ %8.3f ms │                  │ %d trials\n",
           n, n, total_ms / trials, trials);

    tensor_free(a);
    tensor_free(b);
}

/* ---- Benchmark: Full Training Step (forward + backward + SGD) ---- */

static void bench_training_step(int input_dim, int hidden_dim, int output_dim,
                                 int trials) {
    Network *net = network_create();
    network_add_layer(net, input_dim, hidden_dim, ACT_RELU);
    network_add_layer(net, hidden_dim, hidden_dim, ACT_RELU);
    network_add_layer(net, hidden_dim, output_dim, ACT_SIGMOID);

    Optimizer *opt = optimizer_create_sgd(0.01, 0.9, net);

    Tensor *input  = tensor_create(1, input_dim);
    Tensor *target = tensor_create(1, output_dim);
    tensor_rand(input, 0.0, 1.0);
    tensor_rand(target, 0.0, 1.0);

    double total_ms = 0.0;
    for (int t = 0; t < trials; t++) {
        double start = get_time_ms();

        Tensor *output = network_forward(net, input);
        Tensor *grad = loss_mse_deriv(output, target);
        network_backward(net, grad);
        optimizer_step(opt, net);

        double end = get_time_ms();
        total_ms += (end - start);

        tensor_free(output);
        tensor_free(grad);
    }

    printf("  train   %d→%d→%d→%-3d│ %8.3f ms │                  │ %d trials\n",
           input_dim, hidden_dim, hidden_dim, output_dim,
           total_ms / trials, trials);

    tensor_free(input);
    tensor_free(target);
    optimizer_free(opt);
    network_free(net);
}

/* ---- Main ---- */

int main(void) {
    srand(42);  /* Fixed seed for reproducibility */

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   C-Neural-Engine — Performance Benchmarks                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    /* ── Matrix Multiplication ── */
    printf("  ┌─────────────────┬────────────┬──────────────────┬──────────┐\n");
    printf("  │ Operation       │ Avg Time   │ Throughput       │ Trials   │\n");
    printf("  ├─────────────────┼────────────┼──────────────────┼──────────┤\n");

    bench_matmul(32,   1000);
    bench_matmul(64,   500);
    bench_matmul(128,  100);
    bench_matmul(256,  50);
    bench_matmul(512,  10);

    printf("  ├─────────────────┼────────────┼──────────────────┼──────────┤\n");

    /* ── Transpose ── */
    bench_transpose(256, 100);
    bench_transpose(512, 50);

    printf("  ├─────────────────┼────────────┼──────────────────┼──────────┤\n");

    /* ── Element-wise ── */
    bench_add(512, 100);
    bench_hadamard(512, 100);

    printf("  ├─────────────────┼────────────┼──────────────────┼──────────┤\n");

    /* ── Full Training Steps ── */
    bench_training_step(16, 32, 1, 10000);
    bench_training_step(64, 128, 10, 1000);
    bench_training_step(256, 512, 32, 100);

    printf("  └─────────────────┴────────────┴──────────────────┴──────────┘\n\n");

    printf("  ✓ Benchmarks complete.\n\n");
    return 0;
}
