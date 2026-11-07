/* ============================================================================
 * test_main.c — Unified test runner for C-Neural-Engine
 * ============================================================================ */

#include "test_harness.h"

/* Single definition of the harness counters declared `extern` in
 * test_harness.h. All TUs that include the header read/write these. */
int test_count = 0;
int test_failures = 0;
int test_passes = 0;

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
extern void test_tensor_print(void);
extern void test_tensor_free_null(void);

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
extern void test_cross_entropy_deriv_clamp(void);

/* --- Layer tests --- */
extern void test_layer_create(void);
extern void test_layer_weights_initialized(void);
extern void test_layer_forward_shape(void);
extern void test_layer_forward_no_activation(void);
extern void test_layer_forward_relu(void);
extern void test_layer_backward_gradient_shape(void);
extern void test_layer_activation_tanh(void);
extern void test_layer_activation_default(void);
extern void test_layer_free_null(void);

/* --- Network tests --- */
extern void test_network_create(void);
extern void test_network_add_layer(void);
extern void test_network_forward_shape(void);
extern void test_network_backward(void);
extern void test_network_predict(void);
extern void test_network_capacity_realloc(void);
extern void test_network_free_null(void);

/* --- Optimizer tests --- */
extern void test_optimizer_create_sgd(void);
extern void test_optimizer_step_updates_weights(void);
extern void test_optimizer_momentum_accumulates(void);
extern void test_optimizer_velocity_initial_zero(void);
extern void test_optimizer_create_adam(void);
extern void test_optimizer_adam_step(void);
extern void test_optimizer_free_null(void);

/* --- Scenario tests (multi-module integration) --- */
extern void test_layer_caches_after_forward(void);
extern void test_optimizer_reduces_loss_on_linear_regression(void);
extern void test_xor_converges(void);
extern void test_tensor_copy_is_deep(void);
extern void test_tensor_matmul_4x2_2x3(void);

/* --- L3 Limit Order Book tests --- */
extern void test_lob_cache_alignment(void);
extern void test_lob_create_and_free(void);
extern void test_lob_pool_exhaustion(void);
extern void test_lob_clear(void);
extern void test_lob_basic_bbo(void);
extern void test_lob_price_time_priority(void);
extern void test_lob_multi_level_matching_and_depth(void);
extern void test_lob_cancel_orders(void);
extern void test_lob_modify_orders(void);
extern void test_lob_order_types(void);
extern void test_lob_validation_and_errors(void);
extern void test_lob_command_queue(void);

/* --- SIMD Matching Engine tests --- */
extern void test_simd_backend_detection(void);
extern void test_simd_batch_matching_basic(void);
extern void test_simd_batch_matching_permutations(void);
extern void test_simd_batch_matching_overflow(void);
extern void test_simd_price_crossing(void);
extern void test_simd_volume_accumulation(void);
extern void test_simd_level_matching(void);
extern void test_simd_fok_and_market_sweep(void);

/* --- PCAP Binary Parser & Feed Replayer tests --- */
extern void test_pcap_global_header_parsing(void);
extern void test_pcap_writer_and_reader_roundtrip(void);
extern void test_pcap_protocol_decoding(void);
extern void test_pcap_itch_parsing(void);
extern void test_pcap_moldudp64_parsing(void);
extern void test_pcap_feed_to_order_book(void);
extern void test_pcap_replayer_modes(void);
extern void test_pcap_error_handling(void);

/* --- Risk Limit Engine tests --- */
extern void test_risk_validation(void);
extern void test_risk_order_controls(void);
extern void test_risk_margin_reservations(void);
extern void test_risk_execution_loss_reservation(void);
extern void test_risk_position_extremes(void);
extern void test_risk_maintenance_boundary(void);
extern void test_risk_fill_lifecycle(void);
extern void test_risk_margin_breach_and_halt(void);
extern void test_risk_overflow(void);
extern void test_risk_book_integration(void);

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
    RUN_TEST(test_tensor_print);
    RUN_TEST(test_tensor_free_null);

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
    RUN_TEST(test_cross_entropy_deriv_clamp);

    /* ── Layer Module ── */
    RUN_SUITE("Layer: Dense");
    RUN_TEST(test_layer_create);
    RUN_TEST(test_layer_weights_initialized);
    RUN_TEST(test_layer_forward_shape);
    RUN_TEST(test_layer_forward_no_activation);
    RUN_TEST(test_layer_forward_relu);
    RUN_TEST(test_layer_backward_gradient_shape);
    RUN_TEST(test_layer_activation_tanh);
    RUN_TEST(test_layer_activation_default);
    RUN_TEST(test_layer_free_null);

    /* ── Network Module ── */
    RUN_SUITE("Network");
    RUN_TEST(test_network_create);
    RUN_TEST(test_network_add_layer);
    RUN_TEST(test_network_forward_shape);
    RUN_TEST(test_network_backward);
    RUN_TEST(test_network_predict);
    RUN_TEST(test_network_capacity_realloc);
    RUN_TEST(test_network_free_null);

    /* ── Optimizer Module ── */
    RUN_SUITE("Optimizer: SGD & Adam");
    RUN_TEST(test_optimizer_create_sgd);
    RUN_TEST(test_optimizer_step_updates_weights);
    RUN_TEST(test_optimizer_momentum_accumulates);
    RUN_TEST(test_optimizer_velocity_initial_zero);
    RUN_TEST(test_optimizer_create_adam);
    RUN_TEST(test_optimizer_adam_step);
    RUN_TEST(test_optimizer_free_null);

    /* ── Scenario / cross-module ── */
    RUN_SUITE("Scenarios: multi-module integration");
    RUN_TEST(test_layer_caches_after_forward);
    RUN_TEST(test_tensor_copy_is_deep);
    RUN_TEST(test_tensor_matmul_4x2_2x3);
    RUN_TEST(test_optimizer_reduces_loss_on_linear_regression);
    RUN_TEST(test_xor_converges);

    /* ── L3 Limit Order Book Module ── */
    RUN_SUITE("L3 Limit Order Book: Cache Alignment & Memory");
    RUN_TEST(test_lob_cache_alignment);
    RUN_TEST(test_lob_create_and_free);
    RUN_TEST(test_lob_pool_exhaustion);
    RUN_TEST(test_lob_clear);

    RUN_SUITE("L3 Limit Order Book: Matching & Priority");
    RUN_TEST(test_lob_basic_bbo);
    RUN_TEST(test_lob_price_time_priority);
    RUN_TEST(test_lob_multi_level_matching_and_depth);
    RUN_TEST(test_lob_cancel_orders);
    RUN_TEST(test_lob_modify_orders);
    RUN_TEST(test_lob_order_types);
    RUN_TEST(test_lob_validation_and_errors);

    RUN_SUITE("L3 Limit Order Book: Lock-Free Command Queue");
    RUN_TEST(test_lob_command_queue);

    /* ── SIMD Matching Engine Module ── */
    RUN_SUITE("SIMD Matching Engine: Vectorized Instruction Optimizations");
    RUN_TEST(test_simd_backend_detection);
    RUN_TEST(test_simd_batch_matching_basic);
    RUN_TEST(test_simd_batch_matching_permutations);
    RUN_TEST(test_simd_batch_matching_overflow);
    RUN_TEST(test_simd_price_crossing);
    RUN_TEST(test_simd_volume_accumulation);
    RUN_TEST(test_simd_level_matching);
    RUN_TEST(test_simd_fok_and_market_sweep);

    /* ── PCAP Binary Parser & Feed Replayer Module ── */
    RUN_SUITE("PCAP Binary Parser & Nanosecond Feed Replayer");
    RUN_TEST(test_pcap_global_header_parsing);
    RUN_TEST(test_pcap_writer_and_reader_roundtrip);
    RUN_TEST(test_pcap_protocol_decoding);
    RUN_TEST(test_pcap_itch_parsing);
    RUN_TEST(test_pcap_moldudp64_parsing);
    RUN_TEST(test_pcap_feed_to_order_book);
    RUN_TEST(test_pcap_replayer_modes);
    RUN_TEST(test_pcap_error_handling);

    RUN_SUITE("Risk Limit Engine: Pre-trade Controls & Real-time Margin");
    RUN_TEST(test_risk_validation);
    RUN_TEST(test_risk_order_controls);
    RUN_TEST(test_risk_margin_reservations);
    RUN_TEST(test_risk_execution_loss_reservation);
    RUN_TEST(test_risk_position_extremes);
    RUN_TEST(test_risk_maintenance_boundary);
    RUN_TEST(test_risk_fill_lifecycle);
    RUN_TEST(test_risk_margin_breach_and_halt);
    RUN_TEST(test_risk_overflow);
    RUN_TEST(test_risk_book_integration);

    /* ── Report ── */
    TEST_REPORT();

    return test_failures;
}
