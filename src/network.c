/* ============================================================================
 * network.c — Sequential neural network implementation
 * 
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#include "network.h"
#include <stdlib.h>
#include <assert.h>

/* ---- Create ---- */

Network* network_create(void) {
    Network *net = (Network *)malloc(sizeof(Network));
    assert(net != NULL);

    net->num_layers = 0;
    net->capacity   = NETWORK_INIT_CAPACITY;
    net->layers     = (Layer **)malloc((size_t)net->capacity * sizeof(Layer *));
    assert(net->layers != NULL);

    return net;
}

/* ---- Free ---- */

void network_free(Network *net) {
    if (net == NULL) return;
    for (int i = 0; i < net->num_layers; i++) {
        layer_free(net->layers[i]);
    }
    free(net->layers);
    free(net);
}

/* ---- Add Layer ---- */

void network_add_layer(Network *net, int input_size, int output_size,
                       ActivationType act) {
    assert(net != NULL);

    /* Grow array if needed */
    if (net->num_layers >= net->capacity) {
        net->capacity *= 2;
        net->layers = (Layer **)realloc(net->layers,
                                        (size_t)net->capacity * sizeof(Layer *));
        assert(net->layers != NULL);
    }

    Layer *l = layer_create(input_size, output_size, act);
    net->layers[net->num_layers] = l;
    net->num_layers++;
}

/* ---- Forward Pass ---- */

Tensor* network_forward(Network *net, const Tensor *input) {
    assert(net != NULL && input != NULL);
    assert(net->num_layers > 0);

    /* Pass through each layer sequentially */
    Tensor *current = tensor_copy(input);

    for (int i = 0; i < net->num_layers; i++) {
        Tensor *next = layer_forward(net->layers[i], current);
        tensor_free(current);
        current = next;
    }

    return current;
}

/* ---- Backward Pass ---- */

void network_backward(Network *net, const Tensor *d_output) {
    assert(net != NULL && d_output != NULL);
    assert(net->num_layers > 0);

    /* Backpropagate through layers in reverse order */
    Tensor *d_current = tensor_copy(d_output);

    for (int i = net->num_layers - 1; i >= 0; i--) {
        Tensor *d_prev = layer_backward(net->layers[i], d_current);
        tensor_free(d_current);
        d_current = d_prev;
    }

    /* d_current is now d_input (gradient w.r.t. network input) — discard */
    tensor_free(d_current);
}

/* ---- Predict (convenience wrapper around forward) ---- */

Tensor* network_predict(Network *net, const Tensor *input) {
    return network_forward(net, input);
}
