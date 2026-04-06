/* ============================================================================
 * loss.c — Implementation of loss functions
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#include "loss.h"
#include <math.h>
#include <assert.h>

/* Small epsilon to avoid log(0) */
#define LOSS_EPSILON 1e-15

/* ---- Mean Squared Error ---- */

double loss_mse(const Tensor *pred, const Tensor *target) {
    assert(pred != NULL && target != NULL);
    assert(pred->rows == target->rows && pred->cols == target->cols);

    int size = pred->rows * pred->cols;
    double sum = 0.0;
    for (int i = 0; i < size; i++) {
        double diff = pred->data[i] - target->data[i];
        sum += diff * diff;
    }
    return sum / (double)size;
}

Tensor* loss_mse_deriv(const Tensor *pred, const Tensor *target) {
    assert(pred != NULL && target != NULL);
    assert(pred->rows == target->rows && pred->cols == target->cols);

    int size = pred->rows * pred->cols;
    Tensor *grad = tensor_create(pred->rows, pred->cols);
    for (int i = 0; i < size; i++) {
        grad->data[i] = 2.0 * (pred->data[i] - target->data[i]) / (double)size;
    }
    return grad;
}

/* ---- Cross-Entropy Loss ---- */

double loss_cross_entropy(const Tensor *pred, const Tensor *target) {
    assert(pred != NULL && target != NULL);
    assert(pred->rows == target->rows && pred->cols == target->cols);

    int size = pred->rows * pred->cols;
    double sum = 0.0;
    for (int i = 0; i < size; i++) {
        /* Clamp prediction to [epsilon, 1-epsilon] to avoid log(0) */
        double p = pred->data[i];
        if (p < LOSS_EPSILON) p = LOSS_EPSILON;
        if (p > 1.0 - LOSS_EPSILON) p = 1.0 - LOSS_EPSILON;

        sum += target->data[i] * log(p);
    }
    return -sum / (double)size;
}

Tensor* loss_cross_entropy_deriv(const Tensor *pred, const Tensor *target) {
    assert(pred != NULL && target != NULL);
    assert(pred->rows == target->rows && pred->cols == target->cols);

    int size = pred->rows * pred->cols;
    Tensor *grad = tensor_create(pred->rows, pred->cols);
    for (int i = 0; i < size; i++) {
        double p = pred->data[i];
        if (p < LOSS_EPSILON) p = LOSS_EPSILON;
        if (p > 1.0 - LOSS_EPSILON) p = 1.0 - LOSS_EPSILON;

        grad->data[i] = -target->data[i] / p / (double)size;
    }
    return grad;
}
