/* ============================================================================
 * network.h — Sequential neural network model
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#ifndef NETWORK_H
#define NETWORK_H

#include "layer.h"

#define NETWORK_INIT_CAPACITY 8

typedef struct {
    Layer **layers;
    int num_layers;
    int capacity;
} Network;

/* --- API --- */

Network* network_create(void);
void     network_free(Network *net);
void     network_add_layer(Network *net, int input_size, int output_size,
                           ActivationType act);
Tensor*  network_forward(Network *net, const Tensor *input);
void     network_backward(Network *net, const Tensor *d_output);
Tensor*  network_predict(Network *net, const Tensor *input);

#endif /* NETWORK_H */
