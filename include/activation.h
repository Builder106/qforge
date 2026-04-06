/* ============================================================================
 * activation.h — Activation functions and their derivatives
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * All functions return NEW tensors (caller must free).
 * ============================================================================ */

#ifndef ACTIVATION_H
#define ACTIVATION_H

#include "tensor.h"

/* --- ReLU: max(0, x) --- */
Tensor* activation_relu(const Tensor *t);
Tensor* activation_relu_deriv(const Tensor *t);

/* --- Sigmoid: 1 / (1 + exp(-x)) --- */
Tensor* activation_sigmoid(const Tensor *t);
Tensor* activation_sigmoid_deriv(const Tensor *t);

/* --- Tanh --- */
Tensor* activation_tanh_forward(const Tensor *t);
Tensor* activation_tanh_deriv(const Tensor *t);

/* --- Softmax (row-wise, numerically stable) --- */
Tensor* activation_softmax(const Tensor *t);

#endif /* ACTIVATION_H */
