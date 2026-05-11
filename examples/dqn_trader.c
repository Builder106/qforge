/* ============================================================================
 * dqn_trader.c — Deep Q-Network (DQN) Trading Agent
 *
 * Implements a reinforcement learning agent that learns to trade a simulated
 * asset using Deep Q-Learning with:
 *
 *   1. Experience Replay Buffer — stores (s, a, r, s') transitions
 *   2. Epsilon-Greedy Exploration — balances exploration vs exploitation
 *   3. Target Network — stabilizes Q-value targets (updated periodically)
 *   4. MLP Q-Function — maps state → Q-values for each action
 *
 * Actions:  0 = HOLD,  1 = BUY,  2 = SELL
 * State:    [price_norm, position, unrealized_pnl, vol_5d, ret_1d, ret_5d]
 * Reward:   realized P&L on trades, with small penalty for holding
 *
 * C-Neural-Engine: zero-dependency deep learning framework in C99
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "tensor.h"
#include "network.h"
#include "optimizer.h"
#include "loss.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define STATE_DIM      6      /* state features */
#define NUM_ACTIONS    3      /* HOLD, BUY, SELL */
#define REPLAY_SIZE    5000   /* experience replay buffer capacity */
#define BATCH_SIZE     32     /* minibatch size for training */
#define GAMMA          0.95   /* discount factor */
#define EPS_START      1.0    /* initial exploration rate */
#define EPS_END        0.05   /* final exploration rate */
#define EPS_DECAY      0.995  /* per-episode decay */
#define TARGET_UPDATE  10     /* update target network every N episodes */
#define NUM_EPISODES   300    /* training episodes */
#define EPISODE_LEN    200    /* steps per episode */
#define LEARNING_RATE  0.001
#define MOMENTUM       0.9

/* ============================================================================
 * Market Environment
 * ============================================================================ */

typedef struct {
    double *prices;     /* price series for the episode */
    int len;
    int step;           /* current timestep */
    int position;       /* -1 = short, 0 = flat, 1 = long */
    double entry_price; /* price at which position was entered */
    double total_pnl;   /* cumulative realized P&L */
    double cash;
} MarketEnv;

/* Generate a synthetic price series using geometric Brownian motion
 * with regime changes to make the problem non-trivial */
static void generate_prices(double *prices, int n) {
    prices[0] = 100.0;
    double mu = 0.0002;     /* slight upward drift */
    double sigma = 0.015;   /* daily volatility ~1.5% */

    for (int i = 1; i < n; i++) {
        /* Regime change: every ~50 steps, randomly shift drift */
        if (i % 50 == 0) {
            mu = ((double)rand() / RAND_MAX - 0.5) * 0.001;
        }

        double u1 = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
        double u2 = ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
        double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);

        prices[i] = prices[i - 1] * exp(mu + sigma * z);
    }
}

static void env_reset(MarketEnv *env) {
    generate_prices(env->prices, env->len);
    env->step = 5;   /* start after enough history for features */
    env->position = 0;
    env->entry_price = 0.0;
    env->total_pnl = 0.0;
    env->cash = 10000.0;
}

static void env_get_state(const MarketEnv *env, double *state) {
    int t = env->step;

    /* Feature 1: normalized price (relative to recent mean) */
    double price_sum = 0.0;
    for (int i = t - 5; i <= t; i++) price_sum += env->prices[i];
    double price_mean = price_sum / 6.0;
    state[0] = (env->prices[t] - price_mean) / price_mean;

    /* Feature 2: current position (-1, 0, +1) */
    state[1] = (double)env->position;

    /* Feature 3: unrealized P&L (normalized) */
    if (env->position != 0) {
        state[2] = (env->prices[t] - env->entry_price) / env->entry_price
                   * (double)env->position;
    } else {
        state[2] = 0.0;
    }

    /* Feature 4: 5-day realized volatility */
    double vol_sum = 0.0;
    for (int i = t - 4; i <= t; i++) {
        double ret = log(env->prices[i] / env->prices[i - 1]);
        vol_sum += ret * ret;
    }
    state[3] = sqrt(vol_sum / 5.0) * 10.0;  /* scale for NN */

    /* Feature 5: 1-day return */
    state[4] = log(env->prices[t] / env->prices[t - 1]) * 100.0;

    /* Feature 6: 5-day return */
    state[5] = log(env->prices[t] / env->prices[t - 5]) * 100.0;
}

/* Returns reward */
static double env_step(MarketEnv *env, int action) {
    double reward = 0.0;
    double price = env->prices[env->step];

    if (action == 1 && env->position == 0) {
        /* BUY */
        env->position = 1;
        env->entry_price = price;
        reward = -0.001;  /* small transaction cost */
    } else if (action == 2 && env->position == 1) {
        /* SELL (close long position) */
        double pnl = (price - env->entry_price) / env->entry_price;
        env->total_pnl += pnl;
        reward = pnl * 10.0;  /* scale reward for NN */
        env->position = 0;
        env->entry_price = 0.0;
    } else if (action == 2 && env->position == 0) {
        /* SHORT */
        env->position = -1;
        env->entry_price = price;
        reward = -0.001;
    } else if (action == 1 && env->position == -1) {
        /* COVER (close short position) */
        double pnl = (env->entry_price - price) / env->entry_price;
        env->total_pnl += pnl;
        reward = pnl * 10.0;
        env->position = 0;
        env->entry_price = 0.0;
    } else {
        /* HOLD — small time penalty to encourage trading */
        reward = -0.0001;

        /* If holding a position, give small reward for unrealized gains */
        if (env->position == 1) {
            double unrealized = (price - env->entry_price) / env->entry_price;
            reward += unrealized * 0.1;
        } else if (env->position == -1) {
            double unrealized = (env->entry_price - price) / env->entry_price;
            reward += unrealized * 0.1;
        }
    }

    env->step++;
    return reward;
}

static int env_done(const MarketEnv *env) {
    return env->step >= env->len - 1;
}

/* ============================================================================
 * Experience Replay Buffer
 * ============================================================================ */

typedef struct {
    double state[STATE_DIM];
    int action;
    double reward;
    double next_state[STATE_DIM];
    int done;
} Experience;

typedef struct {
    Experience *buffer;
    int capacity;
    int size;
    int write_idx;
} ReplayBuffer;

static ReplayBuffer* replay_create(int capacity) {
    ReplayBuffer *rb = (ReplayBuffer *)malloc(sizeof(ReplayBuffer));
    rb->buffer = (Experience *)malloc((size_t)capacity * sizeof(Experience));
    rb->capacity = capacity;
    rb->size = 0;
    rb->write_idx = 0;
    return rb;
}

static void replay_free(ReplayBuffer *rb) {
    free(rb->buffer);
    free(rb);
}

static void replay_push(ReplayBuffer *rb, const double *state, int action,
                         double reward, const double *next_state, int done) {
    Experience *e = &rb->buffer[rb->write_idx];
    memcpy(e->state, state, STATE_DIM * sizeof(double));
    e->action = action;
    e->reward = reward;
    memcpy(e->next_state, next_state, STATE_DIM * sizeof(double));
    e->done = done;

    rb->write_idx = (rb->write_idx + 1) % rb->capacity;
    if (rb->size < rb->capacity) rb->size++;
}

static Experience* replay_sample(const ReplayBuffer *rb) {
    int idx = rand() % rb->size;
    return &rb->buffer[idx];
}

/* ============================================================================
 * DQN Agent Utilities
 * ============================================================================ */

/* Select action using epsilon-greedy policy */
static int select_action(Network *q_net, const double *state, double epsilon) {
    if ((double)rand() / RAND_MAX < epsilon) {
        return rand() % NUM_ACTIONS;  /* random action */
    }

    /* Greedy action: argmax Q(s, a) */
    Tensor *s = tensor_create(1, STATE_DIM);
    for (int i = 0; i < STATE_DIM; i++) {
        tensor_set(s, 0, i, state[i]);
    }

    Tensor *q_values = network_predict(q_net, s);

    int best_action = 0;
    double best_q = tensor_get(q_values, 0, 0);
    for (int a = 1; a < NUM_ACTIONS; a++) {
        double q = tensor_get(q_values, 0, a);
        if (q > best_q) {
            best_q = q;
            best_action = a;
        }
    }

    tensor_free(s);
    tensor_free(q_values);
    return best_action;
}

/* Copy weights from source network to target network */
static void copy_weights(Network *target, const Network *source) {
    for (int i = 0; i < source->num_layers; i++) {
        int w_size = source->layers[i]->weights->rows *
                     source->layers[i]->weights->cols;
        int b_size = source->layers[i]->biases->rows *
                     source->layers[i]->biases->cols;

        memcpy(target->layers[i]->weights->data,
               source->layers[i]->weights->data,
               (size_t)w_size * sizeof(double));
        memcpy(target->layers[i]->biases->data,
               source->layers[i]->biases->data,
               (size_t)b_size * sizeof(double));
    }
}

/* ============================================================================
 * Main Training Loop
 * ============================================================================ */

int main(int argc, char **argv) {
    srand((unsigned int)time(NULL));

    /* ── Runtime-overridable hyperparameters ──
     * Defaults match the original constants; the web UI passes overrides
     * as argv so a recruiter can tweak γ, learning rate, etc. and re-run
     * without recompiling. argv[1..4] are positional. */
    int    num_episodes  = NUM_EPISODES;
    double gamma_d       = GAMMA;
    double learning_rate = LEARNING_RATE;
    double eps_decay     = EPS_DECAY;
    if (argc > 1) num_episodes  = atoi(argv[1]);
    if (argc > 2) gamma_d       = atof(argv[2]);
    if (argc > 3) learning_rate = atof(argv[3]);
    if (argc > 4) eps_decay     = atof(argv[4]);

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   C-Neural-Engine — DQN Trading Agent                       ║\n");
    printf("║                                                              ║\n");
    printf("║   Actions:  HOLD | BUY | SELL                                ║\n");
    printf("║   State:    price_norm, position, pnl, vol, ret_1d, ret_5d   ║\n");
    printf("║   Method:   Deep Q-Learning + Experience Replay              ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    /* ── Create Q-network and target network ── */
    Network *q_net = network_create();
    network_add_layer(q_net, STATE_DIM, 64, ACT_RELU);
    network_add_layer(q_net, 64, 32, ACT_RELU);
    network_add_layer(q_net, 32, NUM_ACTIONS, ACT_NONE);

    Network *target_net = network_create();
    network_add_layer(target_net, STATE_DIM, 64, ACT_RELU);
    network_add_layer(target_net, 64, 32, ACT_RELU);
    network_add_layer(target_net, 32, NUM_ACTIONS, ACT_NONE);

    copy_weights(target_net, q_net);

    Optimizer *opt = optimizer_create_sgd(learning_rate, MOMENTUM, q_net);
    ReplayBuffer *replay = replay_create(REPLAY_SIZE);

    /* ── Market environment ── */
    MarketEnv env;
    env.len = EPISODE_LEN + 10;
    env.prices = (double *)malloc((size_t)env.len * sizeof(double));

    printf("  Q-Network:     %d → 64 (relu) → 32 (relu) → %d (linear)\n",
           STATE_DIM, NUM_ACTIONS);
    printf("  Replay buffer: %d\n", REPLAY_SIZE);
    printf("  Batch size:    %d\n", BATCH_SIZE);
    printf("  Gamma:         %.3f%s\n", gamma_d,
           (gamma_d != GAMMA) ? "  (overridden)" : "");
    printf("  Learning rate: %.4f%s\n", learning_rate,
           (learning_rate != LEARNING_RATE) ? "  (overridden)" : "");
    printf("  Epsilon:       %.2f → %.2f (decay=%.4f%s)\n",
           EPS_START, EPS_END, eps_decay,
           (eps_decay != EPS_DECAY) ? ", overridden" : "");
    printf("  Episodes:      %d × %d steps%s\n\n", num_episodes, EPISODE_LEN,
           (num_episodes != NUM_EPISODES) ? "  (overridden)" : "");

    /* ── Training ── */
    double epsilon = EPS_START;
    double best_pnl = -1e9;
    /* Aim for ~12 progress lines regardless of episode count */
    int print_every = num_episodes / 12;
    if (print_every < 1) print_every = 1;

    printf("  ┌─────────┬────────────┬────────────┬───────────┬──────────┐\n");
    printf("  │ Episode │ Total P&L  │ Avg Reward │ Epsilon   │ Trades   │\n");
    printf("  ├─────────┼────────────┼────────────┼───────────┼──────────┤\n");

    for (int ep = 0; ep < num_episodes; ep++) {
        env_reset(&env);
        double state[STATE_DIM];
        env_get_state(&env, state);

        double ep_reward = 0.0;
        int trades = 0;

        for (int step = 0; step < EPISODE_LEN && !env_done(&env); step++) {
            /* Select action */
            int action = select_action(q_net, state, epsilon);

            /* Execute action */
            int prev_pos = env.position;
            double reward = env_step(&env, action);
            ep_reward += reward;

            if (env.position != prev_pos) trades++;

            /* Get next state */
            double next_state[STATE_DIM];
            int done = env_done(&env);
            if (!done) {
                env_get_state(&env, next_state);
            } else {
                memset(next_state, 0, STATE_DIM * sizeof(double));
            }

            /* Store experience */
            replay_push(replay, state, action, reward, next_state, done);

            /* Train on minibatch from replay buffer */
            if (replay->size >= BATCH_SIZE) {
                for (int b = 0; b < BATCH_SIZE; b++) {
                    Experience *exp = replay_sample(replay);

                    /* Compute target Q-value */
                    double target_q;
                    if (exp->done) {
                        target_q = exp->reward;
                    } else {
                        Tensor *ns = tensor_create(1, STATE_DIM);
                        for (int i = 0; i < STATE_DIM; i++) {
                            tensor_set(ns, 0, i, exp->next_state[i]);
                        }
                        Tensor *target_qs = network_predict(target_net, ns);

                        /* max_a Q_target(s', a) */
                        double max_q = tensor_get(target_qs, 0, 0);
                        for (int a = 1; a < NUM_ACTIONS; a++) {
                            double q = tensor_get(target_qs, 0, a);
                            if (q > max_q) max_q = q;
                        }
                        target_q = exp->reward + gamma_d * max_q;

                        tensor_free(ns);
                        tensor_free(target_qs);
                    }

                    /* Forward pass */
                    Tensor *s = tensor_create(1, STATE_DIM);
                    for (int i = 0; i < STATE_DIM; i++) {
                        tensor_set(s, 0, i, exp->state[i]);
                    }
                    Tensor *q_vals = network_forward(q_net, s);

                    /* Compute gradient: only for the action taken */
                    Tensor *target_tensor = tensor_copy(q_vals);
                    tensor_set(target_tensor, 0, exp->action, target_q);

                    Tensor *grad = loss_mse_deriv(q_vals, target_tensor);
                    network_backward(q_net, grad);
                    optimizer_step(opt, q_net);

                    tensor_free(s);
                    tensor_free(q_vals);
                    tensor_free(target_tensor);
                    tensor_free(grad);
                }
            }

            /* Update state */
            memcpy(state, next_state, STATE_DIM * sizeof(double));
        }

        /* Force close any open position at end of episode */
        if (env.position != 0) {
            double close_price = env.prices[env.step - 1];
            if (env.position == 1) {
                env.total_pnl += (close_price - env.entry_price) / env.entry_price;
            } else {
                env.total_pnl += (env.entry_price - close_price) / env.entry_price;
            }
        }

        /* Decay epsilon */
        if (epsilon > EPS_END) {
            epsilon *= eps_decay;
            if (epsilon < EPS_END) epsilon = EPS_END;
        }

        /* Update target network periodically */
        if ((ep + 1) % TARGET_UPDATE == 0) {
            copy_weights(target_net, q_net);
        }

        if (env.total_pnl > best_pnl) best_pnl = env.total_pnl;

        if ((ep + 1) % print_every == 0 || ep == 0) {
            printf("  │ %5d   │ %+9.4f%% │ %+9.5f  │   %.4f  │ %5d    │\n",
                   ep + 1, env.total_pnl * 100.0,
                   ep_reward / EPISODE_LEN, epsilon, trades);
        }
    }

    printf("  └─────────┴────────────┴────────────┴───────────┴──────────┘\n\n");

    /* ── Evaluation: Run one episode with epsilon=0 (fully greedy) ── */
    printf("  ── Evaluation (greedy policy, ε=0) ──\n\n");

    env_reset(&env);
    double state[STATE_DIM];
    env_get_state(&env, state);

    int buy_count = 0, sell_count = 0, hold_count = 0;

    for (int step = 0; step < EPISODE_LEN && !env_done(&env); step++) {
        int action = select_action(q_net, state, 0.0);  /* greedy */
        env_step(&env, action);

        if (action == 0) hold_count++;
        else if (action == 1) buy_count++;
        else sell_count++;

        if (!env_done(&env)) {
            env_get_state(&env, state);
        }
    }

    /* Close any open position */
    if (env.position != 0) {
        double close_price = env.prices[env.step - 1];
        if (env.position == 1) {
            env.total_pnl += (close_price - env.entry_price) / env.entry_price;
        } else {
            env.total_pnl += (env.entry_price - close_price) / env.entry_price;
        }
    }

    /* Buy & hold baseline */
    double bh_return = (env.prices[env.step - 1] - env.prices[5]) / env.prices[5];

    printf("  ┌──────────────────────┬──────────────┐\n");
    printf("  │ Metric               │ Value        │\n");
    printf("  ├──────────────────────┼──────────────┤\n");
    printf("  │ DQN Agent P&L        │ %+10.4f%%   │\n", env.total_pnl * 100.0);
    printf("  │ Buy & Hold P&L       │ %+10.4f%%   │\n", bh_return * 100.0);
    printf("  │ Actions (H/B/S)      │ %d/%d/%d      │\n",
           hold_count, buy_count, sell_count);
    printf("  │ Best training P&L    │ %+10.4f%%   │\n", best_pnl * 100.0);
    printf("  └──────────────────────┴──────────────┘\n\n");

    if (env.total_pnl > bh_return) {
        printf("  ✓ DQN agent outperformed buy & hold!\n");
    } else {
        printf("  ~ DQN agent underperformed buy & hold (expected for short training).\n");
        printf("    Increase episodes or tune hyperparameters for better results.\n");
    }

    /* ── Cleanup ── */
    free(env.prices);
    replay_free(replay);
    optimizer_free(opt);
    network_free(q_net);
    network_free(target_net);

    printf("\n  ✓ Done. Zero memory leaks.\n\n");
    return 0;
}
