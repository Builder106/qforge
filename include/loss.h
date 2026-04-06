/* ============================================================================
 * loss.h — Loss functions and their derivatives
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#ifndef LOSS_H
#define LOSS_H

#include "tensor.h"

/* --- Mean Squared Error --- */
double  loss_mse(const Tensor *pred, const Tensor *target);
Tensor* loss_mse_deriv(const Tensor *pred, const Tensor *target);

/* --- Cross-Entropy Loss --- */
double  loss_cross_entropy(const Tensor *pred, const Tensor *target);
Tensor* loss_cross_entropy_deriv(const Tensor *pred, const Tensor *target);

#endif /* LOSS_H */
