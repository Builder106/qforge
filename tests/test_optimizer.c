/* ============================================================================
 * test_optimizer.c — TDD tests for the optimizer module
 * 🔴 RED: Tests written before implementation
 * ============================================================================ */

#include "test_harness.h"
#include "tensor.h"
#include "layer.h"
#include "network.h"
#include "optimizer.h"
#include <math.h>

/* --- SGD Creation --- */

void test_optimizer_create_sgd(void) {
    Network *net = network_create();
    network_add_layer(net, 2, 3, ACT_RELU);
    network_add_layer(net, 3, 1, ACT_NONE);

    Optimizer *opt = optimizer_create_sgd(0.01, 0.0, net);
    ASSERT_NOT_NULL(opt);
    ASSERT_NEAR(opt->learning_rate, 0.01, 1e-9);
    ASSERT_NEAR(opt->momentum, 0.0, 1e-9);
    ASSERT_EQ(opt->num_layers, 2);

    optimizer_free(opt);
    network_free(net);
}

/* --- SGD Step (no momentum) --- */

void test_optimizer_step_updates_weights(void) {
    Network *net = network_create();
    network_add_layer(net, 2, 1, ACT_NONE);

    Optimizer *opt = optimizer_create_sgd(0.1, 0.0, net);

    /* Do a forward + backward to generate gradients */
    Tensor *input = tensor_create(1, 2);
    tensor_set(input, 0, 0, 1.0);
    tensor_set(input, 0, 1, 1.0);

    Tensor *output = network_forward(net, input);

    Tensor *d_output = tensor_create(1, 1);
    tensor_fill(d_output, 1.0);
    network_backward(net, d_output);

    /* Record weights before step */
    double w_before = tensor_get(net->layers[0]->weights, 0, 0);

    /* Take one optimization step */
    optimizer_step(opt, net);

    /* Weights should have changed */
    double w_after = tensor_get(net->layers[0]->weights, 0, 0);
    /*  w_new = w_old - lr * gradient, so if gradient != 0, weight changes */
    ASSERT_TRUE(fabs(w_after - w_before) > 1e-12);

    tensor_free(output);
    tensor_free(d_output);
    tensor_free(input);
    optimizer_free(opt);
    network_free(net);
}

/* --- SGD with Momentum --- */

void test_optimizer_momentum_accumulates(void) {
    Network *net = network_create();
    network_add_layer(net, 2, 1, ACT_NONE);

    Optimizer *opt = optimizer_create_sgd(0.1, 0.9, net);

    Tensor *input = tensor_create(1, 2);
    tensor_set(input, 0, 0, 1.0);
    tensor_set(input, 0, 1, 1.0);

    /* Step 1 */
    Tensor *output1 = network_forward(net, input);
    Tensor *d_out1 = tensor_create(1, 1);
    tensor_fill(d_out1, 1.0);
    network_backward(net, d_out1);

    double w_before_step1 = tensor_get(net->layers[0]->weights, 0, 0);
    optimizer_step(opt, net);
    double w_after_step1 = tensor_get(net->layers[0]->weights, 0, 0);
    double delta1 = fabs(w_after_step1 - w_before_step1);

    /* Step 2 — with momentum, the update should be larger */
    Tensor *output2 = network_forward(net, input);
    Tensor *d_out2 = tensor_create(1, 1);
    tensor_fill(d_out2, 1.0);
    network_backward(net, d_out2);

    double w_before_step2 = tensor_get(net->layers[0]->weights, 0, 0);
    optimizer_step(opt, net);
    double w_after_step2 = tensor_get(net->layers[0]->weights, 0, 0);
    double delta2 = fabs(w_after_step2 - w_before_step2);

    /* With momentum=0.9, step 2 should have a larger update than step 1 */
    ASSERT_TRUE(delta2 > delta1);

    tensor_free(output1); tensor_free(d_out1);
    tensor_free(output2); tensor_free(d_out2);
    tensor_free(input);
    optimizer_free(opt);
    network_free(net);
}

/* --- Velocity initialized to zero --- */

void test_optimizer_velocity_initial_zero(void) {
    Network *net = network_create();
    network_add_layer(net, 4, 3, ACT_RELU);

    Optimizer *opt = optimizer_create_sgd(0.01, 0.9, net);

    /* Velocity tensors should be zero-initialized */
    for (int i = 0; i < 4 * 3; i++) {
        ASSERT_NEAR(opt->velocity_w[0]->data[i], 0.0, 1e-9);
    }
    for (int i = 0; i < 1 * 3; i++) {
        ASSERT_NEAR(opt->velocity_b[0]->data[i], 0.0, 1e-9);
    }

    optimizer_free(opt);
    network_free(net);
}
