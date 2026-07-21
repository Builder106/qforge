/* ============================================================================
 * test_network.c — TDD tests for the network module
 * 🔴 RED: Tests written before implementation
 * ============================================================================ */

#include "test_harness.h"
#include "tensor.h"
#include "layer.h"
#include "network.h"

/* --- Creation --- */

void test_network_create(void) {
    Network *net = network_create();
    ASSERT_NOT_NULL(net);
    ASSERT_EQ(net->num_layers, 0);
    network_free(net);
}

/* --- Adding Layers --- */

void test_network_add_layer(void) {
    Network *net = network_create();
    network_add_layer(net, 3, 4, ACT_RELU);
    network_add_layer(net, 4, 2, ACT_SIGMOID);

    ASSERT_EQ(net->num_layers, 2);

    network_free(net);
}

/* --- Forward Pass --- */

void test_network_forward_shape(void) {
    Network *net = network_create();
    network_add_layer(net, 2, 4, ACT_RELU);
    network_add_layer(net, 4, 1, ACT_SIGMOID);

    Tensor *input = tensor_create(1, 2);
    tensor_set(input, 0, 0, 0.5);
    tensor_set(input, 0, 1, 0.8);

    Tensor *output = network_forward(net, input);
    ASSERT_NOT_NULL(output);
    ASSERT_EQ(output->rows, 1);
    ASSERT_EQ(output->cols, 1);

    /* Sigmoid output should be between 0 and 1 */
    double val = tensor_get(output, 0, 0);
    ASSERT_TRUE(val >= 0.0);
    ASSERT_TRUE(val <= 1.0);

    tensor_free(output);
    tensor_free(input);
    network_free(net);
}

/* --- Backward Pass --- */

void test_network_backward(void) {
    Network *net = network_create();
    network_add_layer(net, 2, 3, ACT_RELU);
    network_add_layer(net, 3, 1, ACT_NONE);

    Tensor *input = tensor_create(1, 2);
    tensor_set(input, 0, 0, 1.0);
    tensor_set(input, 0, 1, 2.0);

    Tensor *output = network_forward(net, input);

    /* Create a dummy gradient */
    Tensor *d_output = tensor_create(1, 1);
    tensor_fill(d_output, 1.0);

    /* Backward should not crash and should populate layer gradients */
    network_backward(net, d_output);

    /* Check that gradients exist in each layer */
    for (int i = 0; i < net->num_layers; i++) {
        ASSERT_NOT_NULL(net->layers[i]->d_weights);
        ASSERT_NOT_NULL(net->layers[i]->d_biases);
    }

    tensor_free(output);
    tensor_free(d_output);
    tensor_free(input);
    network_free(net);
}

/* --- Predict vs Forward (same result, convenience wrapper) --- */

void test_network_predict(void) {
    Network *net = network_create();
    network_add_layer(net, 2, 1, ACT_SIGMOID);

    Tensor *input = tensor_create(1, 2);
    tensor_fill(input, 0.5);

    Tensor *pred = network_predict(net, input);
    ASSERT_NOT_NULL(pred);
    ASSERT_EQ(pred->rows, 1);
    ASSERT_EQ(pred->cols, 1);

    tensor_free(pred);
    tensor_free(input);
    network_free(net);
}

void test_network_capacity_realloc(void) {
    Network *net = network_create();
    
    /* INITIAL_CAPACITY is typically 16. Adding 20 layers should trigger realloc */
    for (int i = 0; i < 20; i++) {
        network_add_layer(net, 2, 2, ACT_NONE);
    }
    
    ASSERT_EQ(net->num_layers, 20);
    ASSERT_TRUE(net->capacity >= 20);
    
    network_free(net);
}
