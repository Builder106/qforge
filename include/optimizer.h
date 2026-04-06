/* ============================================================================
 * optimizer.h — Gradient-based optimizers (SGD, SGD+Momentum)
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "tensor.h"
#include "network.h"

typedef struct {
    double learning_rate;
    double momentum;

    Tensor **velocity_w;    /* momentum terms for weights  */
    Tensor **velocity_b;    /* momentum terms for biases   */
    int num_layers;
} Optimizer;

/* --- API --- */

Optimizer* optimizer_create_sgd(double learning_rate, double momentum,
                                const Network *net);
void       optimizer_step(Optimizer *opt, Network *net);
void       optimizer_free(Optimizer *opt);

#endif /* OPTIMIZER_H */
