/* ============================================================================
 * test_main.c — Unified test runner for C-Neural-Engine
 * ============================================================================ */

#include "test_harness.h"

/* Single definition of the harness counters declared `extern` in
 * test_harness.h. All TUs that include the header read/write these. */
int test_count    = 0;
int test_failures = 0;
int test_passes   = 0;

/* --- Tensor tests --- */
extern void test_tensor_create_dimensions(void);
extern void test_tensor_create_zeroed(void);
extern void test_tensor_create_1x1(void);
extern void test_tensor_get_set(void);
extern void test_tensor_fill(void);
extern void test_tensor_copy(void);
extern void test_tensor_rand(void);
extern void test_tensor_add(void);
extern void test_tensor_sub(void);
extern void test_tensor_mul_scalar(void);
extern void test_tensor_hadamard(void);
extern void test_tensor_matmul_2x3_3x2(void);
extern void test_tensor_matmul_1x1(void);
extern void test_tensor_matmul_identity(void);
extern void test_tensor_transpose(void);
extern void test_tensor_transpose_1x1(void);
extern void test_tensor_add_row_vector(void);
extern void test_tensor_sum_cols(void);

/* --- Activation tests --- */
extern void test_relu_positive(void);
extern void test_relu_negative(void);
extern void test_relu_deriv(void);
extern void test_sigmoid_zero(void);
extern void test_sigmoid_large_positive(void);
extern void test_sigmoid_large_negative(void);
extern void test_sigmoid_deriv(void);
extern void test_tanh_zero(void);
extern void test_tanh_deriv_zero(void);
extern void test_softmax_uniform(void);
extern void test_softmax_sums_to_one(void);
extern void test_softmax_numerical_stability(void);

/* --- Loss tests --- */
extern void test_mse_perfect_prediction(void);
extern void test_mse_known_value(void);
extern void test_mse_deriv(void);
extern void test_cross_entropy_known_value(void);
extern void test_cross_entropy_deriv(void);
extern void test_cross_entropy_clamp(void);

/* --- Layer tests --- */
extern void test_layer_create(void);
extern void test_layer_weights_initialized(void);
extern void test_layer_forward_shape(void);
extern void test_layer_forward_no_activation(void);
extern void test_layer_forward_relu(void);
extern void test_layer_backward_gradient_shape(void);

/* --- Network tests --- */
extern void test_network_create(void);
extern void test_network_add_layer(void);
extern void test_network_forward_shape(void);
extern void test_network_backward(void);
extern void test_network_predict(void);

/* --- Optimizer tests --- */
extern void test_optimizer_create_sgd(void);
extern void test_optimizer_step_updates_weights(void);
extern void test_optimizer_momentum_accumulates(void);
extern void test_optimizer_velocity_initial_zero(void);

/* --- Scenario tests (multi-module integration) --- */
extern void test_layer_caches_after_forward(void);
extern void test_optimizer_reduces_loss_on_linear_regression(void);
extern void test_xor_converges(void);
extern void test_tensor_copy_is_deep(void);
extern void test_tensor_matmul_4x2_2x3(void);

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   C-Neural-Engine — Test Suite           ║\n");
    printf("╚══════════════════════════════════════════╝\n");

    /* ── Tensor Module ── */
    RUN_SUITE("Tensor: Creation & Memory");
    RUN_TEST(test_tensor_create_dimensions);
    RUN_TEST(test_tensor_create_zeroed);
    RUN_TEST(test_tensor_create_1x1);

    RUN_SUITE("Tensor: Get / Set / Fill / Copy / Rand");
    RUN_TEST(test_tensor_get_set);
    RUN_TEST(test_tensor_fill);
    RUN_TEST(test_tensor_copy);
    RUN_TEST(test_tensor_rand);

    RUN_SUITE("Tensor: Arithmetic");
    RUN_TEST(test_tensor_add);
    RUN_TEST(test_tensor_sub);
    RUN_TEST(test_tensor_mul_scalar);
    RUN_TEST(test_tensor_hadamard);

    RUN_SUITE("Tensor: Matrix Multiplication");
    RUN_TEST(test_tensor_matmul_2x3_3x2);
    RUN_TEST(test_tensor_matmul_1x1);
    RUN_TEST(test_tensor_matmul_identity);

    RUN_SUITE("Tensor: Transpose & Broadcasting");
    RUN_TEST(test_tensor_transpose);
    RUN_TEST(test_tensor_transpose_1x1);
    RUN_TEST(test_tensor_add_row_vector);
    RUN_TEST(test_tensor_sum_cols);

    /* ── Activation Module ── */
    RUN_SUITE("Activation: ReLU");
    RUN_TEST(test_relu_positive);
    RUN_TEST(test_relu_negative);
    RUN_TEST(test_relu_deriv);

    RUN_SUITE("Activation: Sigmoid");
    RUN_TEST(test_sigmoid_zero);
    RUN_TEST(test_sigmoid_large_positive);
    RUN_TEST(test_sigmoid_large_negative);
    RUN_TEST(test_sigmoid_deriv);

    RUN_SUITE("Activation: Tanh & Softmax");
    RUN_TEST(test_tanh_zero);
    RUN_TEST(test_tanh_deriv_zero);
    RUN_TEST(test_softmax_uniform);
    RUN_TEST(test_softmax_sums_to_one);
    RUN_TEST(test_softmax_numerical_stability);

    /* ── Loss Module ── */
    RUN_SUITE("Loss: MSE");
    RUN_TEST(test_mse_perfect_prediction);
    RUN_TEST(test_mse_known_value);
    RUN_TEST(test_mse_deriv);

    RUN_SUITE("Loss: Cross-Entropy");
    RUN_TEST(test_cross_entropy_known_value);
    RUN_TEST(test_cross_entropy_deriv);
    RUN_TEST(test_cross_entropy_clamp);

    /* ── Layer Module ── */
    RUN_SUITE("Layer: Dense");
    RUN_TEST(test_layer_create);
    RUN_TEST(test_layer_weights_initialized);
    RUN_TEST(test_layer_forward_shape);
    RUN_TEST(test_layer_forward_no_activation);
    RUN_TEST(test_layer_forward_relu);
    RUN_TEST(test_layer_backward_gradient_shape);

    /* ── Network Module ── */
    RUN_SUITE("Network");
    RUN_TEST(test_network_create);
    RUN_TEST(test_network_add_layer);
    RUN_TEST(test_network_forward_shape);
    RUN_TEST(test_network_backward);
    RUN_TEST(test_network_predict);

    /* ── Optimizer Module ── */
    RUN_SUITE("Optimizer: SGD");
    RUN_TEST(test_optimizer_create_sgd);
    RUN_TEST(test_optimizer_step_updates_weights);
    RUN_TEST(test_optimizer_momentum_accumulates);
    RUN_TEST(test_optimizer_velocity_initial_zero);

    /* ── Scenario / cross-module ── */
    RUN_SUITE("Scenarios: multi-module integration");
    RUN_TEST(test_layer_caches_after_forward);
    RUN_TEST(test_tensor_copy_is_deep);
    RUN_TEST(test_tensor_matmul_4x2_2x3);
    RUN_TEST(test_optimizer_reduces_loss_on_linear_regression);
    RUN_TEST(test_xor_converges);

    /* ── Report ── */
    TEST_REPORT();

    return test_failures;
}
