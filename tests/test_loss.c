/* ============================================================================
 * test_loss.c — TDD tests for loss functions
 * 🔴 RED: Tests written before implementation
 * ============================================================================ */

#include "test_harness.h"
#include "tensor.h"
#include "loss.h"

/* --- MSE --- */

void test_mse_perfect_prediction(void) {
    Tensor *pred = tensor_create(1, 3);
    Tensor *target = tensor_create(1, 3);
    tensor_set(pred, 0, 0, 1.0); tensor_set(pred, 0, 1, 2.0); tensor_set(pred, 0, 2, 3.0);
    tensor_set(target, 0, 0, 1.0); tensor_set(target, 0, 1, 2.0); tensor_set(target, 0, 2, 3.0);

    double loss = loss_mse(pred, target);
    ASSERT_NEAR(loss, 0.0, 1e-9);

    tensor_free(pred); tensor_free(target);
}

void test_mse_known_value(void) {
    /* pred=[1,0], target=[0,1] => MSE = mean((1-0)^2 + (0-1)^2) = mean(1+1) = 1.0 */
    Tensor *pred = tensor_create(1, 2);
    Tensor *target = tensor_create(1, 2);
    tensor_set(pred, 0, 0, 1.0); tensor_set(pred, 0, 1, 0.0);
    tensor_set(target, 0, 0, 0.0); tensor_set(target, 0, 1, 1.0);

    double loss = loss_mse(pred, target);
    ASSERT_NEAR(loss, 1.0, 1e-9);

    tensor_free(pred); tensor_free(target);
}

void test_mse_deriv(void) {
    /* dMSE/dpred = 2*(pred - target) / n */
    Tensor *pred = tensor_create(1, 2);
    Tensor *target = tensor_create(1, 2);
    tensor_set(pred, 0, 0, 3.0); tensor_set(pred, 0, 1, 1.0);
    tensor_set(target, 0, 0, 1.0); tensor_set(target, 0, 1, 1.0);

    Tensor *grad = loss_mse_deriv(pred, target);
    /* grad[0] = 2*(3-1)/2 = 2.0, grad[1] = 2*(1-1)/2 = 0.0 */
    ASSERT_NEAR(tensor_get(grad, 0, 0), 2.0, 1e-6);
    ASSERT_NEAR(tensor_get(grad, 0, 1), 0.0, 1e-6);

    tensor_free(pred); tensor_free(target); tensor_free(grad);
}

/* --- Cross-Entropy --- */

void test_cross_entropy_known_value(void) {
    /* pred=[0.9, 0.1], target=[1, 0] => -[1*ln(0.9) + 0*ln(0.1)] / 2 */
    Tensor *pred = tensor_create(1, 2);
    Tensor *target = tensor_create(1, 2);
    tensor_set(pred, 0, 0, 0.9); tensor_set(pred, 0, 1, 0.1);
    tensor_set(target, 0, 0, 1.0); tensor_set(target, 0, 1, 0.0);

    double loss = loss_cross_entropy(pred, target);
    /* -ln(0.9) / 2 ≈ 0.05268 */
    double expected = -(1.0 * log(0.9) + 0.0 * log(0.1)) / 2.0;
    ASSERT_NEAR(loss, expected, 1e-6);

    tensor_free(pred); tensor_free(target);
}

void test_cross_entropy_deriv(void) {
    Tensor *pred = tensor_create(1, 2);
    Tensor *target = tensor_create(1, 2);
    tensor_set(pred, 0, 0, 0.9); tensor_set(pred, 0, 1, 0.1);
    tensor_set(target, 0, 0, 1.0); tensor_set(target, 0, 1, 0.0);

    Tensor *grad = loss_cross_entropy_deriv(pred, target);
    /* dCE/dpred = -target/pred / n */
    ASSERT_NOT_NULL(grad);
    /* Should be negative for the correct class (target=1) */
    ASSERT_TRUE(tensor_get(grad, 0, 0) < 0);

    tensor_free(pred); tensor_free(target); tensor_free(grad);
}

void test_cross_entropy_clamp(void) {
    /* pred=0 should be clamped to epsilon, not produce -Inf */
    Tensor *pred = tensor_create(1, 2);
    Tensor *target = tensor_create(1, 2);
    tensor_set(pred, 0, 0, 0.0); tensor_set(pred, 0, 1, 1.0);
    tensor_set(target, 0, 0, 1.0); tensor_set(target, 0, 1, 0.0);

    double loss = loss_cross_entropy(pred, target);
    /* Should be a large finite number, not Inf or NaN */
    ASSERT_TRUE(loss > 0.0);
    ASSERT_TRUE(loss < 1e10);

    tensor_free(pred); tensor_free(target);
}
