/* ============================================================================
 * test_layer.c — TDD tests for the dense layer module
 * 🔴 RED: Tests written before implementation
 * ============================================================================ */

#include "test_harness.h"
#include "tensor.h"
#include "layer.h"

/* --- Creation --- */

void test_layer_create(void) {
    Layer *l = layer_create(3, 2, ACT_RELU);
    ASSERT_NOT_NULL(l);
    ASSERT_EQ(l->input_size, 3);
    ASSERT_EQ(l->output_size, 2);
    ASSERT_NOT_NULL(l->weights);
    ASSERT_NOT_NULL(l->biases);
    ASSERT_EQ(l->weights->rows, 3);
    ASSERT_EQ(l->weights->cols, 2);
    ASSERT_EQ(l->biases->rows, 1);
    ASSERT_EQ(l->biases->cols, 2);
    layer_free(l);
}

void test_layer_weights_initialized(void) {
    /* Weights should not all be zero after initialization */
    Layer *l = layer_create(10, 5, ACT_SIGMOID);
    int has_nonzero = 0;
    for (int i = 0; i < l->weights->rows * l->weights->cols; i++) {
        if (fabs(l->weights->data[i]) > 1e-9) {
            has_nonzero = 1;
            break;
        }
    }
    ASSERT_TRUE(has_nonzero);
    layer_free(l);
}

/* --- Forward Pass --- */

void test_layer_forward_shape(void) {
    Layer *l = layer_create(3, 2, ACT_NONE);
    /* Input: batch_size=4, features=3 */
    Tensor *input = tensor_create(4, 3);
    tensor_fill(input, 1.0);

    Tensor *output = layer_forward(l, input);
    ASSERT_NOT_NULL(output);
    ASSERT_EQ(output->rows, 4);
    ASSERT_EQ(output->cols, 2);

    tensor_free(output);
    tensor_free(input);
    layer_free(l);
}

void test_layer_forward_no_activation(void) {
    /* Manual calculation: output = input * weights + bias */
    Layer *l = layer_create(2, 1, ACT_NONE);
    /* Set known weights */
    tensor_set(l->weights, 0, 0, 1.0);
    tensor_set(l->weights, 1, 0, 1.0);
    tensor_set(l->biases, 0, 0, 0.0);

    Tensor *input = tensor_create(1, 2);
    tensor_set(input, 0, 0, 3.0);
    tensor_set(input, 0, 1, 4.0);

    Tensor *output = layer_forward(l, input);
    /* 3*1 + 4*1 + 0 = 7 */
    ASSERT_NEAR(tensor_get(output, 0, 0), 7.0, 1e-6);

    tensor_free(output);
    tensor_free(input);
    layer_free(l);
}

void test_layer_forward_relu(void) {
    Layer *l = layer_create(2, 2, ACT_RELU);
    /* Set weights that produce one positive, one negative pre-activation */
    tensor_set(l->weights, 0, 0, 1.0);  tensor_set(l->weights, 0, 1, -1.0);
    tensor_set(l->weights, 1, 0, 0.0);  tensor_set(l->weights, 1, 1, 0.0);
    tensor_set(l->biases, 0, 0, 0.0);   tensor_set(l->biases, 0, 1, 0.0);

    Tensor *input = tensor_create(1, 2);
    tensor_set(input, 0, 0, 5.0);
    tensor_set(input, 0, 1, 0.0);

    Tensor *output = layer_forward(l, input);
    /* z = [5, -5], relu => [5, 0] */
    ASSERT_NEAR(tensor_get(output, 0, 0), 5.0, 1e-6);
    ASSERT_NEAR(tensor_get(output, 0, 1), 0.0, 1e-6);

    tensor_free(output);
    tensor_free(input);
    layer_free(l);
}

/* --- Backward Pass --- */

void test_layer_backward_gradient_shape(void) {
    Layer *l = layer_create(3, 2, ACT_NONE);
    Tensor *input = tensor_create(1, 3);
    tensor_fill(input, 1.0);

    /* Must do forward first to cache input */
    Tensor *output = layer_forward(l, input);

    /* Gradient flowing back: same shape as output */
    Tensor *d_output = tensor_create(1, 2);
    tensor_fill(d_output, 1.0);

    Tensor *d_input = layer_backward(l, d_output);
    ASSERT_NOT_NULL(d_input);
    ASSERT_EQ(d_input->rows, 1);
    ASSERT_EQ(d_input->cols, 3);

    /* Weight gradients should be populated */
    ASSERT_NOT_NULL(l->d_weights);
    ASSERT_EQ(l->d_weights->rows, 3);
    ASSERT_EQ(l->d_weights->cols, 2);

    ASSERT_NOT_NULL(l->d_biases);
    ASSERT_EQ(l->d_biases->rows, 1);
    ASSERT_EQ(l->d_biases->cols, 2);

    tensor_free(output);
    tensor_free(d_output);
    tensor_free(d_input);
    tensor_free(input);
    layer_free(l);
}
