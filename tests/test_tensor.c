/* ============================================================================
 * test_tensor.c — TDD tests for the Tensor module
 * 🔴 RED: These tests are written BEFORE the implementation.
 * ============================================================================ */

#include "test_harness.h"
#include "tensor.h"
#include <stdlib.h>

/* --- Creation & Memory --- */

void test_tensor_create_dimensions(void) {
    Tensor *t = tensor_create(3, 4);
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(t->rows, 3);
    ASSERT_EQ(t->cols, 4);
    ASSERT_NOT_NULL(t->data);
    tensor_free(t);
}

void test_tensor_create_zeroed(void) {
    /* calloc should zero-initialize */
    Tensor *t = tensor_create(2, 3);
    for (int i = 0; i < 2 * 3; i++) {
        ASSERT_EQ_DBL(t->data[i], 0.0);
    }
    tensor_free(t);
}

void test_tensor_create_1x1(void) {
    Tensor *t = tensor_create(1, 1);
    ASSERT_EQ(t->rows, 1);
    ASSERT_EQ(t->cols, 1);
    tensor_free(t);
}

/* --- Get / Set --- */

void test_tensor_get_set(void) {
    Tensor *t = tensor_create(2, 3);
    tensor_set(t, 0, 0, 1.0);
    tensor_set(t, 0, 2, 3.5);
    tensor_set(t, 1, 1, -2.7);

    ASSERT_NEAR(tensor_get(t, 0, 0), 1.0, 1e-9);
    ASSERT_NEAR(tensor_get(t, 0, 2), 3.5, 1e-9);
    ASSERT_NEAR(tensor_get(t, 1, 1), -2.7, 1e-9);
    /* Unset values should be 0 */
    ASSERT_NEAR(tensor_get(t, 0, 1), 0.0, 1e-9);
    tensor_free(t);
}

/* --- Fill --- */

void test_tensor_fill(void) {
    Tensor *t = tensor_create(3, 3);
    tensor_fill(t, 7.0);
    for (int i = 0; i < 9; i++) {
        ASSERT_NEAR(t->data[i], 7.0, 1e-9);
    }
    tensor_free(t);
}

/* --- Copy --- */

void test_tensor_copy(void) {
    Tensor *t = tensor_create(2, 2);
    tensor_set(t, 0, 0, 1.0);
    tensor_set(t, 0, 1, 2.0);
    tensor_set(t, 1, 0, 3.0);
    tensor_set(t, 1, 1, 4.0);

    Tensor *copy = tensor_copy(t);
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ(copy->rows, 2);
    ASSERT_EQ(copy->cols, 2);
    ASSERT_NEAR(tensor_get(copy, 0, 0), 1.0, 1e-9);
    ASSERT_NEAR(tensor_get(copy, 1, 1), 4.0, 1e-9);

    /* Verify deep copy: modifying original doesn't affect copy */
    tensor_set(t, 0, 0, 99.0);
    ASSERT_NEAR(tensor_get(copy, 0, 0), 1.0, 1e-9);

    tensor_free(t);
    tensor_free(copy);
}

/* --- Random --- */

void test_tensor_rand(void) {
    Tensor *t = tensor_create(10, 10);
    tensor_rand(t, -1.0, 1.0);

    /* All values should be within range */
    for (int i = 0; i < 100; i++) {
        ASSERT_TRUE(t->data[i] >= -1.0);
        ASSERT_TRUE(t->data[i] <= 1.0);
    }

    /* At least one value should be non-zero (statistically near certain) */
    int has_nonzero = 0;
    for (int i = 0; i < 100; i++) {
        if (fabs(t->data[i]) > 1e-9) {
            has_nonzero = 1;
            break;
        }
    }
    ASSERT_TRUE(has_nonzero);
    tensor_free(t);
}

/* --- Element-wise Addition --- */

void test_tensor_add(void) {
    Tensor *a = tensor_create(2, 2);
    Tensor *b = tensor_create(2, 2);
    tensor_set(a, 0, 0, 1.0); tensor_set(a, 0, 1, 2.0);
    tensor_set(a, 1, 0, 3.0); tensor_set(a, 1, 1, 4.0);
    tensor_set(b, 0, 0, 10.0); tensor_set(b, 0, 1, 20.0);
    tensor_set(b, 1, 0, 30.0); tensor_set(b, 1, 1, 40.0);

    Tensor *c = tensor_add(a, b);
    ASSERT_NOT_NULL(c);
    ASSERT_NEAR(tensor_get(c, 0, 0), 11.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 0, 1), 22.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 1, 0), 33.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 1, 1), 44.0, 1e-9);

    tensor_free(a); tensor_free(b); tensor_free(c);
}

/* --- Element-wise Subtraction --- */

void test_tensor_sub(void) {
    Tensor *a = tensor_create(2, 2);
    Tensor *b = tensor_create(2, 2);
    tensor_fill(a, 10.0);
    tensor_fill(b, 3.0);

    Tensor *c = tensor_sub(a, b);
    for (int i = 0; i < 4; i++) {
        ASSERT_NEAR(c->data[i], 7.0, 1e-9);
    }

    tensor_free(a); tensor_free(b); tensor_free(c);
}

/* --- Scalar Multiply --- */

void test_tensor_mul_scalar(void) {
    Tensor *a = tensor_create(2, 3);
    tensor_set(a, 0, 0, 1.0); tensor_set(a, 0, 1, 2.0); tensor_set(a, 0, 2, 3.0);
    tensor_set(a, 1, 0, 4.0); tensor_set(a, 1, 1, 5.0); tensor_set(a, 1, 2, 6.0);

    Tensor *b = tensor_mul_scalar(a, 0.5);
    ASSERT_NEAR(tensor_get(b, 0, 0), 0.5, 1e-9);
    ASSERT_NEAR(tensor_get(b, 1, 2), 3.0, 1e-9);

    tensor_free(a); tensor_free(b);
}

/* --- Hadamard (element-wise) Product --- */

void test_tensor_hadamard(void) {
    Tensor *a = tensor_create(2, 2);
    Tensor *b = tensor_create(2, 2);
    tensor_set(a, 0, 0, 2.0); tensor_set(a, 0, 1, 3.0);
    tensor_set(a, 1, 0, 4.0); tensor_set(a, 1, 1, 5.0);
    tensor_set(b, 0, 0, 10.0); tensor_set(b, 0, 1, 10.0);
    tensor_set(b, 1, 0, 10.0); tensor_set(b, 1, 1, 10.0);

    Tensor *c = tensor_hadamard(a, b);
    ASSERT_NEAR(tensor_get(c, 0, 0), 20.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 0, 1), 30.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 1, 0), 40.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 1, 1), 50.0, 1e-9);

    tensor_free(a); tensor_free(b); tensor_free(c);
}

/* --- Matrix Multiplication --- */

void test_tensor_matmul_2x3_3x2(void) {
    /* A = | 1 2 3 |    B = | 7  8  |    C = A*B = | 58  64  |
     *     | 4 5 6 |        | 9  10 |              | 139 154 |
     *                      | 11 12 |
     */
    Tensor *a = tensor_create(2, 3);
    Tensor *b = tensor_create(3, 2);

    tensor_set(a, 0, 0, 1); tensor_set(a, 0, 1, 2); tensor_set(a, 0, 2, 3);
    tensor_set(a, 1, 0, 4); tensor_set(a, 1, 1, 5); tensor_set(a, 1, 2, 6);

    tensor_set(b, 0, 0, 7);  tensor_set(b, 0, 1, 8);
    tensor_set(b, 1, 0, 9);  tensor_set(b, 1, 1, 10);
    tensor_set(b, 2, 0, 11); tensor_set(b, 2, 1, 12);

    Tensor *c = tensor_matmul(a, b);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(c->rows, 2);
    ASSERT_EQ(c->cols, 2);
    ASSERT_NEAR(tensor_get(c, 0, 0), 58.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 0, 1), 64.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 1, 0), 139.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 1, 1), 154.0, 1e-9);

    tensor_free(a); tensor_free(b); tensor_free(c);
}

void test_tensor_matmul_1x1(void) {
    Tensor *a = tensor_create(1, 1);
    Tensor *b = tensor_create(1, 1);
    tensor_set(a, 0, 0, 3.0);
    tensor_set(b, 0, 0, 7.0);

    Tensor *c = tensor_matmul(a, b);
    ASSERT_NEAR(tensor_get(c, 0, 0), 21.0, 1e-9);

    tensor_free(a); tensor_free(b); tensor_free(c);
}

void test_tensor_matmul_identity(void) {
    /* A * I = A */
    Tensor *a = tensor_create(2, 2);
    tensor_set(a, 0, 0, 5.0); tensor_set(a, 0, 1, 6.0);
    tensor_set(a, 1, 0, 7.0); tensor_set(a, 1, 1, 8.0);

    Tensor *eye = tensor_create(2, 2);
    tensor_set(eye, 0, 0, 1.0); tensor_set(eye, 1, 1, 1.0);

    Tensor *c = tensor_matmul(a, eye);
    ASSERT_NEAR(tensor_get(c, 0, 0), 5.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 0, 1), 6.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 1, 0), 7.0, 1e-9);
    ASSERT_NEAR(tensor_get(c, 1, 1), 8.0, 1e-9);

    tensor_free(a); tensor_free(eye); tensor_free(c);
}

/* --- Transpose --- */

void test_tensor_transpose(void) {
    /* A = | 1 2 3 |    A^T = | 1 4 |
     *     | 4 5 6 |          | 2 5 |
     *                        | 3 6 |
     */
    Tensor *a = tensor_create(2, 3);
    tensor_set(a, 0, 0, 1); tensor_set(a, 0, 1, 2); tensor_set(a, 0, 2, 3);
    tensor_set(a, 1, 0, 4); tensor_set(a, 1, 1, 5); tensor_set(a, 1, 2, 6);

    Tensor *at = tensor_transpose(a);
    ASSERT_NOT_NULL(at);
    ASSERT_EQ(at->rows, 3);
    ASSERT_EQ(at->cols, 2);
    ASSERT_NEAR(tensor_get(at, 0, 0), 1.0, 1e-9);
    ASSERT_NEAR(tensor_get(at, 0, 1), 4.0, 1e-9);
    ASSERT_NEAR(tensor_get(at, 1, 0), 2.0, 1e-9);
    ASSERT_NEAR(tensor_get(at, 1, 1), 5.0, 1e-9);
    ASSERT_NEAR(tensor_get(at, 2, 0), 3.0, 1e-9);
    ASSERT_NEAR(tensor_get(at, 2, 1), 6.0, 1e-9);

    tensor_free(a); tensor_free(at);
}

void test_tensor_transpose_1x1(void) {
    Tensor *a = tensor_create(1, 1);
    tensor_set(a, 0, 0, 42.0);
    Tensor *at = tensor_transpose(a);
    ASSERT_EQ(at->rows, 1);
    ASSERT_EQ(at->cols, 1);
    ASSERT_NEAR(tensor_get(at, 0, 0), 42.0, 1e-9);
    tensor_free(a); tensor_free(at);
}

/* --- Add row vector (broadcast bias) --- */

void test_tensor_add_row_vector(void) {
    /* A = | 1 2 3 |   bias = | 10 20 30 |
     *     | 4 5 6 |
     * Result = | 11 22 33 |
     *          | 14 25 36 |
     */
    Tensor *a = tensor_create(2, 3);
    tensor_set(a, 0, 0, 1); tensor_set(a, 0, 1, 2); tensor_set(a, 0, 2, 3);
    tensor_set(a, 1, 0, 4); tensor_set(a, 1, 1, 5); tensor_set(a, 1, 2, 6);

    Tensor *bias = tensor_create(1, 3);
    tensor_set(bias, 0, 0, 10); tensor_set(bias, 0, 1, 20); tensor_set(bias, 0, 2, 30);

    Tensor *result = tensor_add_row_vector(a, bias);
    ASSERT_NOT_NULL(result);
    ASSERT_NEAR(tensor_get(result, 0, 0), 11.0, 1e-9);
    ASSERT_NEAR(tensor_get(result, 0, 1), 22.0, 1e-9);
    ASSERT_NEAR(tensor_get(result, 0, 2), 33.0, 1e-9);
    ASSERT_NEAR(tensor_get(result, 1, 0), 14.0, 1e-9);
    ASSERT_NEAR(tensor_get(result, 1, 1), 25.0, 1e-9);
    ASSERT_NEAR(tensor_get(result, 1, 2), 36.0, 1e-9);

    tensor_free(a); tensor_free(bias); tensor_free(result);
}

/* --- Sum columns (for bias gradient) --- */

void test_tensor_sum_cols(void) {
    /* A = | 1 2 |   sum_cols = | 3 7 |  (sum of each row => wait, no)
     *     | 5 6 |   Actually sum down columns => | 6 8 | (1x2 vector)
     */
    Tensor *a = tensor_create(2, 2);
    tensor_set(a, 0, 0, 1); tensor_set(a, 0, 1, 2);
    tensor_set(a, 1, 0, 5); tensor_set(a, 1, 1, 6);

    Tensor *s = tensor_sum_cols(a);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->rows, 1);
    ASSERT_EQ(s->cols, 2);
    ASSERT_NEAR(tensor_get(s, 0, 0), 6.0, 1e-9);
    ASSERT_NEAR(tensor_get(s, 0, 1), 8.0, 1e-9);

    tensor_free(a); tensor_free(s);
}
