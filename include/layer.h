/* ============================================================================
 * layer.h — Dense (fully-connected) layer
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#ifndef LAYER_H
#define LAYER_H

#include "tensor.h"

/* --- Activation type enum --- */

typedef enum {
    ACT_NONE,
    ACT_RELU,
    ACT_SIGMOID,
    ACT_TANH
} ActivationType;

/* --- Dense Layer --- */

typedef struct {
    int input_size;
    int output_size;
    ActivationType activation;

    Tensor *weights;        /* [input_size x output_size] */
    Tensor *biases;         /* [1 x output_size]          */

    /* Cached for backward pass */
    Tensor *input_cache;    /* input from last forward     */
    Tensor *z_cache;        /* pre-activation values       */

    /* Gradients (populated by backward) */
    Tensor *d_weights;      /* [input_size x output_size]  */
    Tensor *d_biases;       /* [1 x output_size]           */
} Layer;

/* --- API --- */

Layer*  layer_create(int input_size, int output_size, ActivationType act);
void    layer_free(Layer *l);
Tensor* layer_forward(Layer *l, const Tensor *input);
Tensor* layer_backward(Layer *l, const Tensor *d_output);

#endif /* LAYER_H */
