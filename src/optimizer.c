/* ============================================================================
 * optimizer.c — SGD with momentum and Adam optimizers
 * ============================================================================ */

#include "optimizer.h"
#include <stdlib.h>
#include <assert.h>
#include <math.h>

/* ---- Create SGD ---- */

Optimizer* optimizer_create_sgd(double learning_rate, double momentum,
                                const Network *net) {
    assert(net != NULL);
    assert(learning_rate > 0.0);
    assert(momentum >= 0.0 && momentum < 1.0);

    Optimizer *opt = (Optimizer *)malloc(sizeof(Optimizer));
    assert(opt != NULL);

    opt->type          = OPT_SGD;
    opt->learning_rate = learning_rate;
    opt->momentum      = momentum;
    opt->beta2         = 0.0;
    opt->epsilon       = 1e-8;
    opt->t             = 0;
    opt->num_layers    = net->num_layers;

    opt->velocity_w    = (Tensor **)malloc((size_t)net->num_layers * sizeof(Tensor *));
    opt->velocity_b    = (Tensor **)malloc((size_t)net->num_layers * sizeof(Tensor *));
    opt->sq_velocity_w = NULL;
    opt->sq_velocity_b = NULL;
    assert(opt->velocity_w != NULL && opt->velocity_b != NULL);

    for (int i = 0; i < net->num_layers; i++) {
        Layer *l = net->layers[i];
        opt->velocity_w[i] = tensor_create(l->weights->rows, l->weights->cols);
        opt->velocity_b[i] = tensor_create(l->biases->rows, l->biases->cols);
    }

    return opt;
}

/* ---- Create Adam ---- */

Optimizer* optimizer_create_adam(double learning_rate, double beta1, double beta2,
                                 double epsilon, const Network *net) {
    assert(net != NULL);
    assert(learning_rate > 0.0);
    assert(beta1 >= 0.0 && beta1 < 1.0);
    assert(beta2 >= 0.0 && beta2 < 1.0);
    assert(epsilon > 0.0);

    Optimizer *opt = (Optimizer *)malloc(sizeof(Optimizer));
    assert(opt != NULL);

    opt->type          = OPT_ADAM;
    opt->learning_rate = learning_rate;
    opt->momentum      = beta1;
    opt->beta2         = beta2;
    opt->epsilon       = epsilon;
    opt->t             = 0;
    opt->num_layers    = net->num_layers;

    opt->velocity_w    = (Tensor **)malloc((size_t)net->num_layers * sizeof(Tensor *));
    opt->velocity_b    = (Tensor **)malloc((size_t)net->num_layers * sizeof(Tensor *));
    opt->sq_velocity_w = (Tensor **)malloc((size_t)net->num_layers * sizeof(Tensor *));
    opt->sq_velocity_b = (Tensor **)malloc((size_t)net->num_layers * sizeof(Tensor *));
    assert(opt->velocity_w && opt->velocity_b && opt->sq_velocity_w && opt->sq_velocity_b);

    for (int i = 0; i < net->num_layers; i++) {
        Layer *l = net->layers[i];
        opt->velocity_w[i]    = tensor_create(l->weights->rows, l->weights->cols);
        opt->velocity_b[i]    = tensor_create(l->biases->rows, l->biases->cols);
        opt->sq_velocity_w[i] = tensor_create(l->weights->rows, l->weights->cols);
        opt->sq_velocity_b[i] = tensor_create(l->biases->rows, l->biases->cols);
    }

    return opt;
}

/* ---- Step ---- */

void optimizer_step(Optimizer *opt, Network *net) {
    assert(opt != NULL && net != NULL);
    assert(opt->num_layers == net->num_layers);

    opt->t += 1;

    if (opt->type == OPT_SGD) {
        for (int i = 0; i < net->num_layers; i++) {
            Layer *l = net->layers[i];
            assert(l->d_weights != NULL && l->d_biases != NULL);

            int w_size = l->weights->rows * l->weights->cols;
            int b_size = l->biases->rows * l->biases->cols;

            for (int j = 0; j < w_size; j++) {
                opt->velocity_w[i]->data[j] =
                    opt->momentum * opt->velocity_w[i]->data[j] +
                    l->d_weights->data[j];

                l->weights->data[j] -=
                    opt->learning_rate * opt->velocity_w[i]->data[j];
            }

            for (int j = 0; j < b_size; j++) {
                opt->velocity_b[i]->data[j] =
                    opt->momentum * opt->velocity_b[i]->data[j] +
                    l->d_biases->data[j];

                l->biases->data[j] -=
                    opt->learning_rate * opt->velocity_b[i]->data[j];
            }
        }
    } else if (opt->type == OPT_ADAM) {
        double beta1 = opt->momentum;
        double beta2 = opt->beta2;
        double bias_corr1 = 1.0 - pow(beta1, opt->t);
        double bias_corr2 = 1.0 - pow(beta2, opt->t);

        for (int i = 0; i < net->num_layers; i++) {
            Layer *l = net->layers[i];
            assert(l->d_weights != NULL && l->d_biases != NULL);

            int w_size = l->weights->rows * l->weights->cols;
            int b_size = l->biases->rows * l->biases->cols;

            /* Weights */
            for (int j = 0; j < w_size; j++) {
                double g = l->d_weights->data[j];
                opt->velocity_w[i]->data[j] = beta1 * opt->velocity_w[i]->data[j] + (1.0 - beta1) * g;
                opt->sq_velocity_w[i]->data[j] = beta2 * opt->sq_velocity_w[i]->data[j] + (1.0 - beta2) * g * g;

                double m_hat = opt->velocity_w[i]->data[j] / bias_corr1;
                double v_hat = opt->sq_velocity_w[i]->data[j] / bias_corr2;

                l->weights->data[j] -= opt->learning_rate * m_hat / (sqrt(v_hat) + opt->epsilon);
            }

            /* Biases */
            for (int j = 0; j < b_size; j++) {
                double g = l->d_biases->data[j];
                opt->velocity_b[i]->data[j] = beta1 * opt->velocity_b[i]->data[j] + (1.0 - beta1) * g;
                opt->sq_velocity_b[i]->data[j] = beta2 * opt->sq_velocity_b[i]->data[j] + (1.0 - beta2) * g * g;

                double m_hat = opt->velocity_b[i]->data[j] / bias_corr1;
                double v_hat = opt->sq_velocity_b[i]->data[j] / bias_corr2;

                l->biases->data[j] -= opt->learning_rate * m_hat / (sqrt(v_hat) + opt->epsilon);
            }
        }
    }
}

/* ---- Free ---- */

void optimizer_free(Optimizer *opt) {
    if (opt == NULL) return;
    for (int i = 0; i < opt->num_layers; i++) {
        tensor_free(opt->velocity_w[i]);
        tensor_free(opt->velocity_b[i]);
        if (opt->sq_velocity_w) tensor_free(opt->sq_velocity_w[i]);
        if (opt->sq_velocity_b) tensor_free(opt->sq_velocity_b[i]);
    }
    free(opt->velocity_w);
    free(opt->velocity_b);
    if (opt->sq_velocity_w) free(opt->sq_velocity_w);
    if (opt->sq_velocity_b) free(opt->sq_velocity_b);
    free(opt);
}
