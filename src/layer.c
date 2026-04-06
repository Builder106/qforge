/* ============================================================================
 * layer.c — Dense layer implementation (forward + backward)
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 *
 * Forward:  z = input * weights + bias
 *           output = activation(z)
 *
 * Backward: d_z = d_output ⊙ activation'(z)
 *           d_weights = input^T * d_z
 *           d_biases  = sum_cols(d_z)
 *           d_input   = d_z * weights^T
 * ============================================================================ */

#include "layer.h"
#include "activation.h"
#include <stdlib.h>
#include <math.h>
#include <assert.h>

/* ---- Xavier / He weight initialization ---- */

static void init_weights(Tensor *w, ActivationType act, int fan_in) {
    double scale;
    if (act == ACT_RELU) {
        /* He initialization: sqrt(2 / fan_in) */
        scale = sqrt(2.0 / (double)fan_in);
    } else {
        /* Xavier initialization: sqrt(1 / fan_in) */
        scale = sqrt(1.0 / (double)fan_in);
    }
    tensor_rand(w, -scale, scale);
}

/* ---- Create ---- */

Layer* layer_create(int input_size, int output_size, ActivationType act) {
    assert(input_size > 0 && output_size > 0);

    Layer *l = (Layer *)malloc(sizeof(Layer));
    assert(l != NULL);

    l->input_size  = input_size;
    l->output_size = output_size;
    l->activation  = act;

    l->weights = tensor_create(input_size, output_size);
    l->biases  = tensor_create(1, output_size);

    init_weights(l->weights, act, input_size);
    /* Biases start at zero */

    l->input_cache = NULL;
    l->z_cache     = NULL;
    l->d_weights   = NULL;
    l->d_biases    = NULL;

    return l;
}

/* ---- Free ---- */

void layer_free(Layer *l) {
    if (l == NULL) return;
    tensor_free(l->weights);
    tensor_free(l->biases);
    tensor_free(l->input_cache);
    tensor_free(l->z_cache);
    tensor_free(l->d_weights);
    tensor_free(l->d_biases);
    free(l);
}

/* ---- Apply activation (returns new tensor) ---- */

static Tensor* apply_activation(const Tensor *z, ActivationType act) {
    switch (act) {
        case ACT_RELU:    return activation_relu(z);
        case ACT_SIGMOID: return activation_sigmoid(z);
        case ACT_TANH:    return activation_tanh_forward(z);
        case ACT_NONE:    return tensor_copy(z);
        default:          return tensor_copy(z);
    }
}

/* ---- Apply activation derivative (returns new tensor) ---- */

static Tensor* apply_activation_deriv(const Tensor *z, ActivationType act) {
    switch (act) {
        case ACT_RELU:    return activation_relu_deriv(z);
        case ACT_SIGMOID: return activation_sigmoid_deriv(z);
        case ACT_TANH:    return activation_tanh_deriv(z);
        case ACT_NONE: {
            /* Derivative of identity is 1 everywhere */
            Tensor *ones = tensor_create(z->rows, z->cols);
            tensor_fill(ones, 1.0);
            return ones;
        }
        default: {
            Tensor *ones = tensor_create(z->rows, z->cols);
            tensor_fill(ones, 1.0);
            return ones;
        }
    }
}

/* ---- Forward Pass ---- */

Tensor* layer_forward(Layer *l, const Tensor *input) {
    assert(l != NULL && input != NULL);
    assert(input->cols == l->input_size);

    /* Cache the input for backward pass */
    tensor_free(l->input_cache);
    l->input_cache = tensor_copy(input);

    /* z = input * weights + bias */
    Tensor *matmul_result = tensor_matmul(input, l->weights);
    Tensor *z = tensor_add_row_vector(matmul_result, l->biases);
    tensor_free(matmul_result);

    /* Cache z for backward pass */
    tensor_free(l->z_cache);
    l->z_cache = tensor_copy(z);

    /* output = activation(z) */
    Tensor *output = apply_activation(z, l->activation);
    tensor_free(z);

    return output;
}

/* ---- Backward Pass ---- */

Tensor* layer_backward(Layer *l, const Tensor *d_output) {
    assert(l != NULL && d_output != NULL);
    assert(l->z_cache != NULL && l->input_cache != NULL);

    /* d_z = d_output ⊙ activation'(z_cache) */
    Tensor *act_deriv = apply_activation_deriv(l->z_cache, l->activation);
    Tensor *d_z = tensor_hadamard(d_output, act_deriv);
    tensor_free(act_deriv);

    /* d_weights = input_cache^T * d_z */
    Tensor *input_t = tensor_transpose(l->input_cache);
    tensor_free(l->d_weights);
    l->d_weights = tensor_matmul(input_t, d_z);
    tensor_free(input_t);

    /* d_biases = sum_cols(d_z) */
    tensor_free(l->d_biases);
    l->d_biases = tensor_sum_cols(d_z);

    /* d_input = d_z * weights^T */
    Tensor *weights_t = tensor_transpose(l->weights);
    Tensor *d_input = tensor_matmul(d_z, weights_t);
    tensor_free(weights_t);
    tensor_free(d_z);

    return d_input;
}
