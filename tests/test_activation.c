/* ============================================================================
 * test_activation.c — TDD tests for activation functions
 * 🔴 RED: Tests written before implementation
 * ============================================================================ */

#include "test_harness.h"
#include "tensor.h"
#include "activation.h"

/* --- ReLU --- */

void test_relu_positive(void) {
    Tensor *t = tensor_create(1, 3);
    tensor_set(t, 0, 0, 1.0);
    tensor_set(t, 0, 1, 5.0);
    tensor_set(t, 0, 2, 0.5);

    Tensor *r = activation_relu(t);
    ASSERT_NEAR(tensor_get(r, 0, 0), 1.0, 1e-9);
    ASSERT_NEAR(tensor_get(r, 0, 1), 5.0, 1e-9);
    ASSERT_NEAR(tensor_get(r, 0, 2), 0.5, 1e-9);

    tensor_free(t); tensor_free(r);
}

void test_relu_negative(void) {
    Tensor *t = tensor_create(1, 3);
    tensor_set(t, 0, 0, -1.0);
    tensor_set(t, 0, 1, -100.0);
    tensor_set(t, 0, 2, 0.0);

    Tensor *r = activation_relu(t);
    ASSERT_NEAR(tensor_get(r, 0, 0), 0.0, 1e-9);
    ASSERT_NEAR(tensor_get(r, 0, 1), 0.0, 1e-9);
    ASSERT_NEAR(tensor_get(r, 0, 2), 0.0, 1e-9);

    tensor_free(t); tensor_free(r);
}

void test_relu_deriv(void) {
    Tensor *t = tensor_create(1, 4);
    tensor_set(t, 0, 0, -2.0);
    tensor_set(t, 0, 1, 0.0);
    tensor_set(t, 0, 2, 3.0);
    tensor_set(t, 0, 3, 0.001);

    Tensor *d = activation_relu_deriv(t);
    ASSERT_NEAR(tensor_get(d, 0, 0), 0.0, 1e-9);
    ASSERT_NEAR(tensor_get(d, 0, 1), 0.0, 1e-9);
    ASSERT_NEAR(tensor_get(d, 0, 2), 1.0, 1e-9);
    ASSERT_NEAR(tensor_get(d, 0, 3), 1.0, 1e-9);

    tensor_free(t); tensor_free(d);
}

/* --- Sigmoid --- */

void test_sigmoid_zero(void) {
    Tensor *t = tensor_create(1, 1);
    tensor_set(t, 0, 0, 0.0);

    Tensor *s = activation_sigmoid(t);
    ASSERT_NEAR(tensor_get(s, 0, 0), 0.5, 1e-6);

    tensor_free(t); tensor_free(s);
}

void test_sigmoid_large_positive(void) {
    Tensor *t = tensor_create(1, 1);
    tensor_set(t, 0, 0, 100.0);

    Tensor *s = activation_sigmoid(t);
    ASSERT_NEAR(tensor_get(s, 0, 0), 1.0, 1e-6);

    tensor_free(t); tensor_free(s);
}

void test_sigmoid_large_negative(void) {
    Tensor *t = tensor_create(1, 1);
    tensor_set(t, 0, 0, -100.0);

    Tensor *s = activation_sigmoid(t);
    ASSERT_NEAR(tensor_get(s, 0, 0), 0.0, 1e-6);

    tensor_free(t); tensor_free(s);
}

void test_sigmoid_deriv(void) {
    /* sigmoid_deriv(x) = sigmoid(x) * (1 - sigmoid(x)) */
    /* At x=0: sigmoid(0) = 0.5, so deriv = 0.25 */
    Tensor *t = tensor_create(1, 1);
    tensor_set(t, 0, 0, 0.0);

    Tensor *d = activation_sigmoid_deriv(t);
    ASSERT_NEAR(tensor_get(d, 0, 0), 0.25, 1e-6);

    tensor_free(t); tensor_free(d);
}

/* --- Tanh --- */

void test_tanh_zero(void) {
    Tensor *t = tensor_create(1, 1);
    tensor_set(t, 0, 0, 0.0);

    Tensor *r = activation_tanh_forward(t);
    ASSERT_NEAR(tensor_get(r, 0, 0), 0.0, 1e-6);

    tensor_free(t); tensor_free(r);
}

void test_tanh_deriv_zero(void) {
    /* tanh_deriv(0) = 1 - tanh²(0) = 1 - 0 = 1 */
    Tensor *t = tensor_create(1, 1);
    tensor_set(t, 0, 0, 0.0);

    Tensor *d = activation_tanh_deriv(t);
    ASSERT_NEAR(tensor_get(d, 0, 0), 1.0, 1e-6);

    tensor_free(t); tensor_free(d);
}

/* --- Softmax --- */

void test_softmax_uniform(void) {
    /* Equal inputs => equal probabilities => 1/3 each */
    Tensor *t = tensor_create(1, 3);
    tensor_fill(t, 1.0);

    Tensor *s = activation_softmax(t);
    ASSERT_NEAR(tensor_get(s, 0, 0), 1.0 / 3.0, 1e-6);
    ASSERT_NEAR(tensor_get(s, 0, 1), 1.0 / 3.0, 1e-6);
    ASSERT_NEAR(tensor_get(s, 0, 2), 1.0 / 3.0, 1e-6);

    tensor_free(t); tensor_free(s);
}

void test_softmax_sums_to_one(void) {
    Tensor *t = tensor_create(1, 4);
    tensor_set(t, 0, 0, 1.0);
    tensor_set(t, 0, 1, 2.0);
    tensor_set(t, 0, 2, 3.0);
    tensor_set(t, 0, 3, 4.0);

    Tensor *s = activation_softmax(t);
    double sum = 0.0;
    for (int j = 0; j < 4; j++) {
        sum += tensor_get(s, 0, j);
    }
    ASSERT_NEAR(sum, 1.0, 1e-6);

    tensor_free(t); tensor_free(s);
}

void test_softmax_numerical_stability(void) {
    /* Very large values — naive softmax would overflow without subtracting max */
    Tensor *t = tensor_create(1, 3);
    tensor_set(t, 0, 0, 1000.0);
    tensor_set(t, 0, 1, 1000.0);
    tensor_set(t, 0, 2, 1000.0);

    Tensor *s = activation_softmax(t);
    /* Should not be NaN or Inf */
    ASSERT_NEAR(tensor_get(s, 0, 0), 1.0 / 3.0, 1e-6);
    ASSERT_NEAR(tensor_get(s, 0, 1), 1.0 / 3.0, 1e-6);

    tensor_free(t); tensor_free(s);
}
