/* ============================================================================
 * gradient_check.c — Numerical gradient verification via finite differences
 *
 * Compares analytical gradients (from backpropagation) against numerical
 * gradients computed via the central difference formula:
 *
 *     dL/dw ≈ [L(w + ε) - L(w - ε)] / (2ε)
 *
 * If the relative error between analytical and numerical gradients is small
 * (< 1e-5), backpropagation is implemented correctly.
 *
 * This is standard practice in ML research and quantitative modeling to
 * verify correctness of derivative computations.
 *
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "tensor.h"
#include "layer.h"
#include "network.h"
#include "loss.h"

#define EPSILON 1e-5
#define TOLERANCE 1e-4

/* ---- Compute loss for a given network state ---- */

static double compute_loss(Network *net, const Tensor *input,
                           const Tensor *target) {
    Tensor *output = network_forward(net, input);
    double loss = loss_mse(output, target);
    tensor_free(output);
    return loss;
}

/* ---- Gradient check for a single layer's weights ---- */

static int check_layer_weights(Network *net, int layer_idx,
                                const Tensor *input, const Tensor *target) {
    Layer *l = net->layers[layer_idx];
    int total_params = l->weights->rows * l->weights->cols;
    int passed = 0;
    int failed = 0;
    double max_rel_error = 0.0;

    /* First: compute analytical gradients via backprop */
    Tensor *output = network_forward(net, input);
    Tensor *d_loss = loss_mse_deriv(output, target);
    network_backward(net, d_loss);
    tensor_free(output);
    tensor_free(d_loss);

    /* Now compare each weight's analytical gradient against numerical */
    for (int i = 0; i < total_params; i++) {
        double analytical = l->d_weights->data[i];

        /* Numerical gradient: central difference */
        double original = l->weights->data[i];

        /* L(w + ε) */
        l->weights->data[i] = original + EPSILON;
        double loss_plus = compute_loss(net, input, target);

        /* L(w - ε) */
        l->weights->data[i] = original - EPSILON;
        double loss_minus = compute_loss(net, input, target);

        /* Restore original weight */
        l->weights->data[i] = original;

        double numerical = (loss_plus - loss_minus) / (2.0 * EPSILON);

        /* Relative error (avoid divide-by-zero) */
        double denominator = fmax(fabs(analytical), fabs(numerical));
        double rel_error;
        if (denominator < 1e-12) {
            rel_error = 0.0;  /* Both near zero — gradient is correct */
        } else {
            rel_error = fabs(analytical - numerical) / denominator;
        }

        if (rel_error > max_rel_error) {
            max_rel_error = rel_error;
        }

        if (rel_error < TOLERANCE) {
            passed++;
        } else {
            failed++;
            if (failed <= 5) {  /* Print first 5 failures */
                printf("    ✗ w[%d]: analytical=%.8f  numerical=%.8f  "
                       "rel_err=%.2e\n", i, analytical, numerical, rel_error);
            }
        }
    }

    printf("    Layer %d weights: %d/%d passed  (max rel error: %.2e)\n",
           layer_idx, passed, total_params, max_rel_error);

    return failed;
}

/* ---- Gradient check for a single layer's biases ---- */

static int check_layer_biases(Network *net, int layer_idx,
                               const Tensor *input, const Tensor *target) {
    Layer *l = net->layers[layer_idx];
    int total_params = l->biases->rows * l->biases->cols;
    int passed = 0;
    int failed = 0;
    double max_rel_error = 0.0;

    /* Compute analytical gradients */
    Tensor *output = network_forward(net, input);
    Tensor *d_loss = loss_mse_deriv(output, target);
    network_backward(net, d_loss);
    tensor_free(output);
    tensor_free(d_loss);

    for (int i = 0; i < total_params; i++) {
        double analytical = l->d_biases->data[i];
        double original = l->biases->data[i];

        l->biases->data[i] = original + EPSILON;
        double loss_plus = compute_loss(net, input, target);

        l->biases->data[i] = original - EPSILON;
        double loss_minus = compute_loss(net, input, target);

        l->biases->data[i] = original;

        double numerical = (loss_plus - loss_minus) / (2.0 * EPSILON);

        double denominator = fmax(fabs(analytical), fabs(numerical));
        double rel_error;
        if (denominator < 1e-12) {
            rel_error = 0.0;
        } else {
            rel_error = fabs(analytical - numerical) / denominator;
        }

        if (rel_error > max_rel_error) max_rel_error = rel_error;

        if (rel_error < TOLERANCE) {
            passed++;
        } else {
            failed++;
            if (failed <= 3) {
                printf("    ✗ b[%d]: analytical=%.8f  numerical=%.8f  "
                       "rel_err=%.2e\n", i, analytical, numerical, rel_error);
            }
        }
    }

    printf("    Layer %d biases:  %d/%d passed  (max rel error: %.2e)\n",
           layer_idx, passed, total_params, max_rel_error);

    return failed;
}

/* ---- Main ---- */

int main(void) {
    srand(42);

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   C-Neural-Engine — Numerical Gradient Verification         ║\n");
    printf("║                                                              ║\n");
    printf("║   Method: Central Finite Differences                         ║\n");
    printf("║   Formula: dL/dw ≈ [L(w+ε) - L(w-ε)] / 2ε                  ║\n");
    printf("║   Epsilon: %.1e    Tolerance: %.1e                     ║\n",
           EPSILON, TOLERANCE);
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    int total_failures = 0;

    /* ── Test 1: Simple 2→3→1 network with Sigmoid ── */
    {
        printf("  ── Test 1: Network [2 → 3 (sigmoid) → 1 (sigmoid)] ──\n\n");

        Network *net = network_create();
        network_add_layer(net, 2, 3, ACT_SIGMOID);
        network_add_layer(net, 3, 1, ACT_SIGMOID);

        Tensor *input = tensor_create(1, 2);
        tensor_set(input, 0, 0, 0.5);
        tensor_set(input, 0, 1, -0.3);

        Tensor *target = tensor_create(1, 1);
        tensor_set(target, 0, 0, 0.8);

        for (int i = 0; i < net->num_layers; i++) {
            total_failures += check_layer_weights(net, i, input, target);
            total_failures += check_layer_biases(net, i, input, target);
        }

        tensor_free(input);
        tensor_free(target);
        network_free(net);
    }

    printf("\n");

    /* ── Test 2: Deeper network with ReLU ── */
    {
        printf("  ── Test 2: Network [4 → 8 (relu) → 4 (relu) → 2 (sigmoid)] ──\n\n");

        Network *net = network_create();
        network_add_layer(net, 4, 8, ACT_RELU);
        network_add_layer(net, 8, 4, ACT_RELU);
        network_add_layer(net, 4, 2, ACT_SIGMOID);

        Tensor *input = tensor_create(1, 4);
        tensor_set(input, 0, 0, 0.1);
        tensor_set(input, 0, 1, -0.5);
        tensor_set(input, 0, 2, 0.8);
        tensor_set(input, 0, 3, 0.3);

        Tensor *target = tensor_create(1, 2);
        tensor_set(target, 0, 0, 1.0);
        tensor_set(target, 0, 1, 0.0);

        for (int i = 0; i < net->num_layers; i++) {
            total_failures += check_layer_weights(net, i, input, target);
            total_failures += check_layer_biases(net, i, input, target);
        }

        tensor_free(input);
        tensor_free(target);
        network_free(net);
    }

    printf("\n");

    /* ── Test 3: Linear network (no activation) ── */
    {
        printf("  ── Test 3: Network [3 → 5 (none) → 2 (none)] ──\n\n");

        Network *net = network_create();
        network_add_layer(net, 3, 5, ACT_NONE);
        network_add_layer(net, 5, 2, ACT_NONE);

        Tensor *input = tensor_create(1, 3);
        tensor_set(input, 0, 0, 1.0);
        tensor_set(input, 0, 1, -1.0);
        tensor_set(input, 0, 2, 0.5);

        Tensor *target = tensor_create(1, 2);
        tensor_set(target, 0, 0, 2.0);
        tensor_set(target, 0, 1, -1.5);

        for (int i = 0; i < net->num_layers; i++) {
            total_failures += check_layer_weights(net, i, input, target);
            total_failures += check_layer_biases(net, i, input, target);
        }

        tensor_free(input);
        tensor_free(target);
        network_free(net);
    }

    printf("\n");

    /* ── Test 4: Tanh activation ── */
    {
        printf("  ── Test 4: Network [2 → 4 (tanh) → 1 (sigmoid)] ──\n\n");

        Network *net = network_create();
        network_add_layer(net, 2, 4, ACT_TANH);
        network_add_layer(net, 4, 1, ACT_SIGMOID);

        Tensor *input = tensor_create(1, 2);
        tensor_set(input, 0, 0, 0.7);
        tensor_set(input, 0, 1, -0.2);

        Tensor *target = tensor_create(1, 1);
        tensor_set(target, 0, 0, 0.5);

        for (int i = 0; i < net->num_layers; i++) {
            total_failures += check_layer_weights(net, i, input, target);
            total_failures += check_layer_biases(net, i, input, target);
        }

        tensor_free(input);
        tensor_free(target);
        network_free(net);
    }

    /* ── Summary ── */
    printf("\n");
    printf("  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    if (total_failures == 0) {
        printf("  ✓ All gradient checks passed!\n");
        printf("    Backpropagation is numerically correct.\n");
    } else {
        printf("  ✗ %d gradient check(s) failed.\n", total_failures);
        printf("    Review backpropagation implementation.\n");
    }
    printf("  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    return total_failures > 0 ? 1 : 0;
}
