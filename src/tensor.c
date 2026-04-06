/* ============================================================================
 * tensor.c — Implementation of Tensor operations
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#include "tensor.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/* ---- Lifecycle ---- */

Tensor* tensor_create(int rows, int cols) {
    assert(rows > 0 && cols > 0);

    Tensor *t = (Tensor *)malloc(sizeof(Tensor));
    assert(t != NULL);

    t->rows = rows;
    t->cols = cols;
    t->data = (double *)calloc((size_t)(rows * cols), sizeof(double));
    assert(t->data != NULL);

    return t;
}

void tensor_free(Tensor *t) {
    if (t != NULL) {
        free(t->data);
        free(t);
    }
}

Tensor* tensor_copy(const Tensor *t) {
    assert(t != NULL);

    Tensor *copy = tensor_create(t->rows, t->cols);
    memcpy(copy->data, t->data, (size_t)(t->rows * t->cols) * sizeof(double));
    return copy;
}

/* ---- Accessors ---- */

double tensor_get(const Tensor *t, int row, int col) {
    assert(t != NULL);
    assert(row >= 0 && row < t->rows);
    assert(col >= 0 && col < t->cols);
    return t->data[row * t->cols + col];
}

void tensor_set(Tensor *t, int row, int col, double val) {
    assert(t != NULL);
    assert(row >= 0 && row < t->rows);
    assert(col >= 0 && col < t->cols);
    t->data[row * t->cols + col] = val;
}

/* ---- Initializers ---- */

void tensor_fill(Tensor *t, double val) {
    assert(t != NULL);
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        t->data[i] = val;
    }
}

void tensor_rand(Tensor *t, double lo, double hi) {
    assert(t != NULL);
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        double r = (double)rand() / (double)RAND_MAX;  /* [0, 1] */
        t->data[i] = lo + r * (hi - lo);
    }
}

/* ---- Element-wise Operations ---- */

Tensor* tensor_add(const Tensor *a, const Tensor *b) {
    assert(a != NULL && b != NULL);
    assert(a->rows == b->rows && a->cols == b->cols);

    Tensor *result = tensor_create(a->rows, a->cols);
    int size = a->rows * a->cols;
    for (int i = 0; i < size; i++) {
        result->data[i] = a->data[i] + b->data[i];
    }
    return result;
}

Tensor* tensor_sub(const Tensor *a, const Tensor *b) {
    assert(a != NULL && b != NULL);
    assert(a->rows == b->rows && a->cols == b->cols);

    Tensor *result = tensor_create(a->rows, a->cols);
    int size = a->rows * a->cols;
    for (int i = 0; i < size; i++) {
        result->data[i] = a->data[i] - b->data[i];
    }
    return result;
}

Tensor* tensor_mul_scalar(const Tensor *t, double scalar) {
    assert(t != NULL);

    Tensor *result = tensor_create(t->rows, t->cols);
    int size = t->rows * t->cols;
    for (int i = 0; i < size; i++) {
        result->data[i] = t->data[i] * scalar;
    }
    return result;
}

Tensor* tensor_hadamard(const Tensor *a, const Tensor *b) {
    assert(a != NULL && b != NULL);
    assert(a->rows == b->rows && a->cols == b->cols);

    Tensor *result = tensor_create(a->rows, a->cols);
    int size = a->rows * a->cols;
    for (int i = 0; i < size; i++) {
        result->data[i] = a->data[i] * b->data[i];
    }
    return result;
}

/* ---- Matrix Operations ---- */

Tensor* tensor_matmul(const Tensor *a, const Tensor *b) {
    assert(a != NULL && b != NULL);
    assert(a->cols == b->rows);

    Tensor *result = tensor_create(a->rows, b->cols);

    for (int i = 0; i < a->rows; i++) {
        for (int j = 0; j < b->cols; j++) {
            double sum = 0.0;
            for (int k = 0; k < a->cols; k++) {
                sum += a->data[i * a->cols + k] * b->data[k * b->cols + j];
            }
            result->data[i * b->cols + j] = sum;
        }
    }
    return result;
}

Tensor* tensor_transpose(const Tensor *t) {
    assert(t != NULL);

    Tensor *result = tensor_create(t->cols, t->rows);
    for (int i = 0; i < t->rows; i++) {
        for (int j = 0; j < t->cols; j++) {
            result->data[j * t->rows + i] = t->data[i * t->cols + j];
        }
    }
    return result;
}

/* ---- Broadcasting Operations ---- */

Tensor* tensor_add_row_vector(const Tensor *mat, const Tensor *row_vec) {
    assert(mat != NULL && row_vec != NULL);
    assert(row_vec->rows == 1);
    assert(row_vec->cols == mat->cols);

    Tensor *result = tensor_create(mat->rows, mat->cols);
    for (int i = 0; i < mat->rows; i++) {
        for (int j = 0; j < mat->cols; j++) {
            result->data[i * mat->cols + j] =
                mat->data[i * mat->cols + j] + row_vec->data[j];
        }
    }
    return result;
}

Tensor* tensor_sum_cols(const Tensor *t) {
    assert(t != NULL);

    Tensor *result = tensor_create(1, t->cols);
    for (int j = 0; j < t->cols; j++) {
        double sum = 0.0;
        for (int i = 0; i < t->rows; i++) {
            sum += t->data[i * t->cols + j];
        }
        result->data[j] = sum;
    }
    return result;
}

/* ---- Debug ---- */

void tensor_print(const Tensor *t, const char *name) {
    if (name != NULL) {
        printf("%s (%dx%d):\n", name, t->rows, t->cols);
    }
    for (int i = 0; i < t->rows; i++) {
        printf("  [");
        for (int j = 0; j < t->cols; j++) {
            printf(" %8.4f", t->data[i * t->cols + j]);
        }
        printf(" ]\n");
    }
    printf("\n");
}
