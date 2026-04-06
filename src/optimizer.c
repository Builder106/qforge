/* ============================================================================
 * optimizer.c — SGD optimizer with optional momentum
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 *
 * SGD update rule:
 *   v = momentum * v + gradient
 *   w = w - learning_rate * v
 *
 * When momentum = 0, this reduces to vanilla SGD:
 *   w = w - learning_rate * gradient
 * ============================================================================ */

#include "optimizer.h"
#include <stdlib.h>
#include <assert.h>

/* ---- Create ---- */

Optimizer* optimizer_create_sgd(double learning_rate, double momentum,
                                const Network *net) {
    assert(net != NULL);
    assert(learning_rate > 0.0);
    assert(momentum >= 0.0 && momentum < 1.0);

    Optimizer *opt = (Optimizer *)malloc(sizeof(Optimizer));
    assert(opt != NULL);

    opt->learning_rate = learning_rate;
    opt->momentum      = momentum;
    opt->num_layers    = net->num_layers;

    /* Allocate velocity tensors (zero-initialized via tensor_create/calloc) */
    opt->velocity_w = (Tensor **)malloc((size_t)net->num_layers * sizeof(Tensor *));
    opt->velocity_b = (Tensor **)malloc((size_t)net->num_layers * sizeof(Tensor *));
    assert(opt->velocity_w != NULL && opt->velocity_b != NULL);

    for (int i = 0; i < net->num_layers; i++) {
        Layer *l = net->layers[i];
        opt->velocity_w[i] = tensor_create(l->weights->rows, l->weights->cols);
        opt->velocity_b[i] = tensor_create(l->biases->rows, l->biases->cols);
    }

    return opt;
}

/* ---- Step ---- */

void optimizer_step(Optimizer *opt, Network *net) {
    assert(opt != NULL && net != NULL);
    assert(opt->num_layers == net->num_layers);

    for (int i = 0; i < net->num_layers; i++) {
        Layer *l = net->layers[i];
        assert(l->d_weights != NULL && l->d_biases != NULL);

        int w_size = l->weights->rows * l->weights->cols;
        int b_size = l->biases->rows * l->biases->cols;

        /* Update weights: v = momentum*v + grad; w = w - lr*v */
        for (int j = 0; j < w_size; j++) {
            opt->velocity_w[i]->data[j] =
                opt->momentum * opt->velocity_w[i]->data[j] +
                l->d_weights->data[j];

            l->weights->data[j] -=
                opt->learning_rate * opt->velocity_w[i]->data[j];
        }

        /* Update biases: v = momentum*v + grad; b = b - lr*v */
        for (int j = 0; j < b_size; j++) {
            opt->velocity_b[i]->data[j] =
                opt->momentum * opt->velocity_b[i]->data[j] +
                l->d_biases->data[j];

            l->biases->data[j] -=
                opt->learning_rate * opt->velocity_b[i]->data[j];
        }
    }
}

/* ---- Free ---- */

void optimizer_free(Optimizer *opt) {
    if (opt == NULL) return;
    for (int i = 0; i < opt->num_layers; i++) {
        tensor_free(opt->velocity_w[i]);
        tensor_free(opt->velocity_b[i]);
    }
    free(opt->velocity_w);
    free(opt->velocity_b);
    free(opt);
}
