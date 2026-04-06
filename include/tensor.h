/* ============================================================================
 * tensor.h — Tensor (Matrix) data structure and linear algebra operations
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * All tensors are 2D (rows × cols), stored row-major in a flat double array.
 * ============================================================================ */

#ifndef TENSOR_H
#define TENSOR_H

#include <stdio.h>

/* --- Data Structure --- */

typedef struct {
    int rows;
    int cols;
    double *data;   /* row-major: element [i][j] = data[i * cols + j] */
} Tensor;

/* --- Lifecycle --- */

Tensor* tensor_create(int rows, int cols);
void    tensor_free(Tensor *t);
Tensor* tensor_copy(const Tensor *t);

/* --- Accessors --- */

double tensor_get(const Tensor *t, int row, int col);
void   tensor_set(Tensor *t, int row, int col, double val);

/* --- Initializers --- */

void tensor_fill(Tensor *t, double val);
void tensor_rand(Tensor *t, double lo, double hi);

/* --- Element-wise Operations (return new tensor) --- */

Tensor* tensor_add(const Tensor *a, const Tensor *b);
Tensor* tensor_sub(const Tensor *a, const Tensor *b);
Tensor* tensor_mul_scalar(const Tensor *t, double scalar);
Tensor* tensor_hadamard(const Tensor *a, const Tensor *b);

/* --- Matrix Operations --- */

Tensor* tensor_matmul(const Tensor *a, const Tensor *b);
Tensor* tensor_transpose(const Tensor *t);

/* --- Broadcasting Operations (for neural network layers) --- */

Tensor* tensor_add_row_vector(const Tensor *mat, const Tensor *row_vec);
Tensor* tensor_sum_cols(const Tensor *t);

/* --- Debug --- */

void tensor_print(const Tensor *t, const char *name);

#endif /* TENSOR_H */
