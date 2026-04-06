/* ============================================================================
 * activation.c — Implementation of activation functions
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#include "activation.h"
#include <math.h>
#include <assert.h>

/* ---- ReLU ---- */

Tensor* activation_relu(const Tensor *t) {
    assert(t != NULL);
    Tensor *result = tensor_create(t->rows, t->cols);
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        result->data[i] = t->data[i] > 0.0 ? t->data[i] : 0.0;
    }
    return result;
}

Tensor* activation_relu_deriv(const Tensor *t) {
    assert(t != NULL);
    Tensor *result = tensor_create(t->rows, t->cols);
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        result->data[i] = t->data[i] > 0.0 ? 1.0 : 0.0;
    }
    return result;
}

/* ---- Sigmoid ---- */

static double sigmoid_scalar(double x) {
    /* Numerically stable sigmoid */
    if (x >= 0.0) {
        return 1.0 / (1.0 + exp(-x));
    } else {
        double ex = exp(x);
        return ex / (1.0 + ex);
    }
}

Tensor* activation_sigmoid(const Tensor *t) {
    assert(t != NULL);
    Tensor *result = tensor_create(t->rows, t->cols);
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        result->data[i] = sigmoid_scalar(t->data[i]);
    }
    return result;
}

Tensor* activation_sigmoid_deriv(const Tensor *t) {
    assert(t != NULL);
    Tensor *result = tensor_create(t->rows, t->cols);
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        double s = sigmoid_scalar(t->data[i]);
        result->data[i] = s * (1.0 - s);
    }
    return result;
}

/* ---- Tanh ---- */

Tensor* activation_tanh_forward(const Tensor *t) {
    assert(t != NULL);
    Tensor *result = tensor_create(t->rows, t->cols);
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        result->data[i] = tanh(t->data[i]);
    }
    return result;
}

Tensor* activation_tanh_deriv(const Tensor *t) {
    assert(t != NULL);
    Tensor *result = tensor_create(t->rows, t->cols);
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        double th = tanh(t->data[i]);
        result->data[i] = 1.0 - th * th;
    }
    return result;
}

/* ---- Softmax (row-wise, numerically stable) ---- */

Tensor* activation_softmax(const Tensor *t) {
    assert(t != NULL);
    Tensor *result = tensor_create(t->rows, t->cols);

    for (int i = 0; i < t->rows; i++) {
        /* Find row max for numerical stability */
        double row_max = t->data[i * t->cols];
        for (int j = 1; j < t->cols; j++) {
            double val = t->data[i * t->cols + j];
            if (val > row_max) row_max = val;
        }

        /* Compute exp(x - max) and sum */
        double sum = 0.0;
        for (int j = 0; j < t->cols; j++) {
            double val = exp(t->data[i * t->cols + j] - row_max);
            result->data[i * t->cols + j] = val;
            sum += val;
        }

        /* Normalize */
        for (int j = 0; j < t->cols; j++) {
            result->data[i * t->cols + j] /= sum;
        }
    }
    return result;
}
