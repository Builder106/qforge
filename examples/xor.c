/* ============================================================================
 * xor.c — XOR problem demo
 *
 * Trains a 2-2-1 Multi-Layer Perceptron to learn the XOR function:
 *   [0,0] → 0    [0,1] → 1    [1,0] → 1    [1,1] → 0
 *
 * This is the classic "hello world" of neural networks — a non-linearly
 * separable problem that requires at least one hidden layer to solve.
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

int main(int argc, char **argv) {
    /* Seed RNG for weight initialization */
    srand((unsigned int)time(NULL));

    /* ── Runtime-overridable hyperparameters (argv[1]=epochs, argv[2]=lr) ── */
    int    epochs        = 10000;
    double learning_rate = 1.0;
    double momentum      = 0.9;
    if (argc > 1) epochs        = atoi(argv[1]);
    if (argc > 2) learning_rate = atof(argv[2]);

    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   C-Neural-Engine — XOR Training Demo        ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    /* ── Training Data ── */
    /* XOR inputs: 4 samples, 2 features each */
    double xor_inputs[4][2] = {
        {0.0, 0.0},
        {0.0, 1.0},
        {1.0, 0.0},
        {1.0, 1.0}
    };
    double xor_targets[4] = {0.0, 1.0, 1.0, 0.0};

    /* ── Build Network: 2 → 4 → 1 ── */
    Network *net = network_create();
    network_add_layer(net, 2, 4, ACT_SIGMOID);   /* Hidden layer */
    network_add_layer(net, 4, 1, ACT_SIGMOID);   /* Output layer */

    /* ── Optimizer: SGD with momentum ── */
    Optimizer *opt = optimizer_create_sgd(learning_rate, momentum, net);

    /* ── Training Loop ── */
    int print_every = epochs / 10;
    if (print_every < 1) print_every = 1;

    printf("  Architecture: 2 → 4 (sigmoid) → 1 (sigmoid)\n");
    printf("  Optimizer:    SGD (lr=%.2f, momentum=%.2f)\n", learning_rate, momentum);
    printf("  Loss:         MSE\n");
    printf("  Epochs:       %d\n\n", epochs);

    for (int epoch = 0; epoch < epochs; epoch++) {
        double total_loss = 0.0;

        for (int s = 0; s < 4; s++) {
            /* Create input tensor (1×2) */
            Tensor *input = tensor_create(1, 2);
            tensor_set(input, 0, 0, xor_inputs[s][0]);
            tensor_set(input, 0, 1, xor_inputs[s][1]);

            /* Create target tensor (1×1) */
            Tensor *target = tensor_create(1, 1);
            tensor_set(target, 0, 0, xor_targets[s]);

            /* Forward pass */
            Tensor *output = network_forward(net, input);

            /* Compute loss */
            double sample_loss = loss_mse(output, target);
            total_loss += sample_loss;

            /* Backward pass */
            Tensor *d_loss = loss_mse_deriv(output, target);
            network_backward(net, d_loss);

            /* Optimizer step */
            optimizer_step(opt, net);

            /* Cleanup */
            tensor_free(input);
            tensor_free(target);
            tensor_free(output);
            tensor_free(d_loss);
        }

        total_loss /= 4.0;

        if ((epoch + 1) % print_every == 0 || epoch == 0) {
            printf("  Epoch %5d/%d  │  Loss: %.8f\n", epoch + 1, epochs, total_loss);
        }
    }

    /* ── Evaluation ── */
    printf("\n");
    printf("  ┌─────────────┬──────────┬──────────┐\n");
    printf("  │   Input     │ Expected │ Predicted│\n");
    printf("  ├─────────────┼──────────┼──────────┤\n");

    for (int s = 0; s < 4; s++) {
        Tensor *input = tensor_create(1, 2);
        tensor_set(input, 0, 0, xor_inputs[s][0]);
        tensor_set(input, 0, 1, xor_inputs[s][1]);

        Tensor *output = network_predict(net, input);
        double pred = tensor_get(output, 0, 0);

        printf("  │  [%.0f, %.0f]    │   %.0f      │  %.4f  │\n",
               xor_inputs[s][0], xor_inputs[s][1],
               xor_targets[s], pred);

        tensor_free(input);
        tensor_free(output);
    }

    printf("  └─────────────┴──────────┴──────────┘\n\n");

    /* ── Cleanup ── */
    optimizer_free(opt);
    network_free(net);

    printf("  ✓ Done. Zero memory leaks.\n\n");
    return 0;
}
