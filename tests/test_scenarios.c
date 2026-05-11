/* ============================================================================
 * test_scenarios.c — small end-to-end "scenario" tests
 *
 * These are still unit tests in the sense that they run in-process via the
 * harness, but each one wires up multiple modules (tensor + layer + network
 * + loss + optimizer) and asserts on emergent behavior: caching invariants,
 * loss decreasing under gradient descent, and convergence of a tiny problem
 * with known optima. The layer-N integration suite covers compiled binaries;
 * this file covers the same engine path *without* the example wrappers.
 * ============================================================================ */

#include "test_harness.h"
#include "tensor.h"
#include "layer.h"
#include "network.h"
#include "loss.h"
#include "optimizer.h"
#include <stdlib.h>
#include <math.h>

/* ---- Layer caching invariants ---- */

void test_layer_caches_after_forward(void) {
    Layer *l = layer_create(3, 2, ACT_SIGMOID);
    Tensor *input = tensor_create(1, 3);
    tensor_set(input, 0, 0, 0.1);
    tensor_set(input, 0, 1, 0.2);
    tensor_set(input, 0, 2, 0.3);

    /* Before forward: caches must be empty (initial state from layer_create) */
    ASSERT_NULL(l->input_cache);
    ASSERT_NULL(l->z_cache);

    Tensor *out = layer_forward(l, input);

    /* After forward: caches must exist with the right shapes */
    ASSERT_NOT_NULL(l->input_cache);
    ASSERT_EQ(l->input_cache->rows, 1);
    ASSERT_EQ(l->input_cache->cols, 3);
    ASSERT_NOT_NULL(l->z_cache);
    ASSERT_EQ(l->z_cache->rows, 1);
    ASSERT_EQ(l->z_cache->cols, 2);

    /* input_cache must be a value copy, not aliased to the caller's tensor */
    ASSERT_NEAR(tensor_get(l->input_cache, 0, 1), 0.2, 1e-12);
    tensor_set(input, 0, 1, 99.0);
    ASSERT_NEAR(tensor_get(l->input_cache, 0, 1), 0.2, 1e-12);

    tensor_free(out);
    tensor_free(input);
    layer_free(l);
}

/* ---- Optimizer reduces loss on a convex problem ----
 *
 * Tiny linear regression: a 1→1 network with no activation should learn the
 * mapping y = 2x from four samples. After enough SGD steps the MSE should
 * be orders of magnitude smaller than the initial loss. This catches
 * regressions where forward/backward/optimizer fall out of sync. */

void test_optimizer_reduces_loss_on_linear_regression(void) {
    srand(42);

    Network *net = network_create();
    network_add_layer(net, 1, 1, ACT_NONE);
    Optimizer *opt = optimizer_create_sgd(0.05, 0.0, net);

    double xs[4] = { 0.0, 1.0, 2.0, 3.0 };
    double ys[4] = { 0.0, 2.0, 4.0, 6.0 };

    double initial_loss = 0.0;
    double final_loss   = 0.0;

    for (int epoch = 0; epoch < 500; epoch++) {
        double epoch_loss = 0.0;
        for (int s = 0; s < 4; s++) {
            Tensor *in  = tensor_create(1, 1); tensor_set(in,  0, 0, xs[s]);
            Tensor *tgt = tensor_create(1, 1); tensor_set(tgt, 0, 0, ys[s]);

            Tensor *pred = network_forward(net, in);
            epoch_loss += loss_mse(pred, tgt);
            Tensor *grad = loss_mse_deriv(pred, tgt);
            network_backward(net, grad);
            optimizer_step(opt, net);

            tensor_free(in); tensor_free(tgt); tensor_free(pred); tensor_free(grad);
        }
        if (epoch == 0)   initial_loss = epoch_loss / 4.0;
        if (epoch == 499) final_loss   = epoch_loss / 4.0;
    }

    ASSERT_TRUE(initial_loss > 0.0);
    ASSERT_TRUE(final_loss < initial_loss * 0.001);  /* >1000× reduction */
    ASSERT_TRUE(final_loss < 1e-3);

    optimizer_free(opt);
    network_free(net);
}

/* ---- Network converges on XOR (the canonical non-linearly-separable case) ----
 *
 * Mirrors the headline example: a 2→4→1 MLP with sigmoid activations should
 * converge to <0.01 MSE within 5000 epochs at lr=1.0. If this test fails,
 * something fundamental is broken — and the home-page demo would too. */

void test_xor_converges(void) {
    srand(7);

    Network *net = network_create();
    network_add_layer(net, 2, 4, ACT_SIGMOID);
    network_add_layer(net, 4, 1, ACT_SIGMOID);
    Optimizer *opt = optimizer_create_sgd(1.0, 0.9, net);

    double inputs[4][2] = {{0,0},{0,1},{1,0},{1,1}};
    double targets[4]   = { 0,   1,   1,   0   };

    for (int epoch = 0; epoch < 5000; epoch++) {
        for (int s = 0; s < 4; s++) {
            Tensor *in  = tensor_create(1, 2);
            tensor_set(in, 0, 0, inputs[s][0]); tensor_set(in, 0, 1, inputs[s][1]);
            Tensor *tgt = tensor_create(1, 1); tensor_set(tgt, 0, 0, targets[s]);

            Tensor *pred = network_forward(net, in);
            Tensor *grad = loss_mse_deriv(pred, tgt);
            network_backward(net, grad);
            optimizer_step(opt, net);

            tensor_free(in); tensor_free(tgt); tensor_free(pred); tensor_free(grad);
        }
    }

    /* Verify final predictions are on the correct side of 0.5 */
    int correct = 0;
    for (int s = 0; s < 4; s++) {
        Tensor *in = tensor_create(1, 2);
        tensor_set(in, 0, 0, inputs[s][0]); tensor_set(in, 0, 1, inputs[s][1]);
        Tensor *out = network_predict(net, in);
        double p = tensor_get(out, 0, 0);
        if ((p > 0.5) == (targets[s] > 0.5)) correct++;
        tensor_free(in); tensor_free(out);
    }
    ASSERT_EQ(correct, 4);

    optimizer_free(opt);
    network_free(net);
}

/* ---- Tensor copy is fully independent ----
 *
 * Sanity check that tensor_copy() returns a deep copy — modifying the
 * source must not perturb the copy. Used implicitly by Layer's input
 * caching; if this regresses, caching breaks silently. */

void test_tensor_copy_is_deep(void) {
    Tensor *src = tensor_create(2, 3);
    tensor_set(src, 0, 0, 1.0);
    tensor_set(src, 1, 2, 7.0);

    Tensor *copy = tensor_copy(src);
    ASSERT_NEAR(tensor_get(copy, 0, 0), 1.0, 1e-12);
    ASSERT_NEAR(tensor_get(copy, 1, 2), 7.0, 1e-12);

    /* Mutating src must not change copy */
    tensor_set(src, 0, 0, 999.0);
    ASSERT_NEAR(tensor_get(copy, 0, 0), 1.0, 1e-12);

    /* And vice versa */
    tensor_set(copy, 1, 2, -1.0);
    ASSERT_NEAR(tensor_get(src, 1, 2), 7.0, 1e-12);

    tensor_free(src);
    tensor_free(copy);
}

/* ---- Matmul with non-square + non-symmetric shapes ----
 *
 * 4×2 · 2×3 → 4×3 verifies the general case beyond the existing 2×3 · 3×2
 * test. Catches mistakes where someone "fixed" matmul to special-case
 * square inputs. */

void test_tensor_matmul_4x2_2x3(void) {
    Tensor *a = tensor_create(4, 2);
    Tensor *b = tensor_create(2, 3);

    /* a = [[1,2],[3,4],[5,6],[7,8]] */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 2; j++)
            tensor_set(a, i, j, (double)(i * 2 + j + 1));

    /* b = [[1,0,2],[0,1,3]] */
    tensor_set(b, 0, 0, 1); tensor_set(b, 0, 1, 0); tensor_set(b, 0, 2, 2);
    tensor_set(b, 1, 0, 0); tensor_set(b, 1, 1, 1); tensor_set(b, 1, 2, 3);

    Tensor *c = tensor_matmul(a, b);
    ASSERT_EQ(c->rows, 4);
    ASSERT_EQ(c->cols, 3);

    /* Hand-computed: row i = [a_{i,0}, a_{i,1} | a_{i,0}*2 + a_{i,1}*3 ] */
    ASSERT_NEAR(tensor_get(c, 0, 0), 1.0, 1e-12);
    ASSERT_NEAR(tensor_get(c, 0, 1), 2.0, 1e-12);
    ASSERT_NEAR(tensor_get(c, 0, 2), 1*2 + 2*3, 1e-12);
    ASSERT_NEAR(tensor_get(c, 3, 0), 7.0, 1e-12);
    ASSERT_NEAR(tensor_get(c, 3, 2), 7*2 + 8*3, 1e-12);

    tensor_free(a); tensor_free(b); tensor_free(c);
}
