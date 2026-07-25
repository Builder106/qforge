/* ============================================================================
 * optimizer.h — Gradient-based optimizers (SGD, SGD+Momentum)
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "tensor.h"
#include "network.h"

typedef enum {
    OPT_SGD,
    OPT_ADAM
} OptimizerType;

typedef struct {
    OptimizerType type;
    double learning_rate;
    double momentum;   /* momentum for SGD, beta1 for Adam */
    double beta2;      /* beta2 for Adam */
    double epsilon;    /* epsilon for Adam */
    int t;             /* timestep count for Adam bias correction */

    Tensor **velocity_w;    /* velocity/m_w for weights  */
    Tensor **velocity_b;    /* velocity/m_b for biases   */
    Tensor **sq_velocity_w; /* v_w (second moment) for Adam weights */
    Tensor **sq_velocity_b; /* v_b (second moment) for Adam biases  */
    int num_layers;
} Optimizer;

/* --- API --- */

Optimizer* optimizer_create_sgd(double learning_rate, double momentum,
                                const Network *net);
Optimizer* optimizer_create_adam(double learning_rate, double beta1, double beta2,
                                 double epsilon, const Network *net);
void       optimizer_step(Optimizer *opt, Network *net);
void       optimizer_free(Optimizer *opt);

#endif /* OPTIMIZER_H */
