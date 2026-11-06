/* ============================================================================
 * order_book.h — Lock-Free Cache-Aligned L3 Limit Order Book
 *
 * Implements a Level 3 (Market-By-Order / MBO) limit order book optimized for
 * sub-microsecond algorithmic trading. Key architectural properties:
 *
 * 1. Cache Alignment: Core data structures (lob_order_t, lob_price_level_t)
 *    are 64-byte aligned and sized to fit exactly in modern CPU cache lines,
 *    preventing false sharing and minimizing L1/L2 cache misses.
 *
 * 2. Lock-Free Memory Pools: Orders and price levels are pre-allocated in
 *    contiguous cache-aligned arenas and managed via lock-free Treiber stacks
 *    with 32-bit atomic tags to prevent ABA hazards. Zero dynamic allocations
 *    occur on the critical order processing path.
 *
 * 3. Lock-Free Ring Buffer: Multi-Producer / Multi-Consumer (MPMC) bounded
 *    command queue based on Dmitry Vyukov's algorithm for lock-free order
 *    submission from concurrent gateway threads.
 *
 * 4. Deterministic O(1) Operations: Price-time priority matching (FIFO),
 *    O(1) order lookup/cancellation via open-addressing backward-shift map,
 *    and O(1) best bid / best ask traversal.
 * ============================================================================ */

#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Cache Line Alignment
 * ---------------------------------------------------------------------------- */
#define LOB_CACHE_LINE 64

#if defined(__GNUC__) || defined(__clang__)
#define LOB_ALIGNED(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
#define LOB_ALIGNED(n) __declspec(align(n))
#else
#define LOB_ALIGNED(n)
#endif

/* ----------------------------------------------------------------------------
 * Enumerations
 * ---------------------------------------------------------------------------- */

typedef enum { LOB_SIDE_BUY = 0, LOB_SIDE_SELL = 1 } lob_side_t;

typedef enum {
    LOB_STATUS_ACTIVE = 0,
    LOB_STATUS_FILLED = 1,
    LOB_STATUS_CANCELLED = 2,
    LOB_STATUS_REJECTED = 3
} lob_status_t;

typedef enum {
    LOB_TYPE_LIMIT = 0,     /* Standard resting limit order */
    LOB_TYPE_IOC = 1,       /* Immediate-Or-Cancel: fill available, cancel rest */
    LOB_TYPE_FOK = 2,       /* Fill-Or-Kill: fill entire order or reject completely */
    LOB_TYPE_POST_ONLY = 3, /* Maker only: reject if order would cross/take */
    LOB_TYPE_MARKET = 4     /* Aggressive market match at prevailing prices */
} lob_type_t;

typedef enum {
    LOB_RESULT_OK = 0,
    LOB_RESULT_INVALID_ARG = -1,
    LOB_RESULT_DUPLICATE_ORDER_ID = -2,
    LOB_RESULT_ORDER_NOT_FOUND = -3,
    LOB_RESULT_POOL_EXHAUSTED = -4,
    LOB_RESULT_POST_ONLY_CROSSED = -5,
    LOB_RESULT_FOK_NOT_FILLED = -6
} lob_result_t;

typedef enum { LOB_CMD_ADD = 0, LOB_CMD_CANCEL = 1, LOB_CMD_MODIFY = 2 } lob_cmd_type_t;

/* ----------------------------------------------------------------------------
 * Trade / Match Execution Event
 * ---------------------------------------------------------------------------- */

typedef struct {
    uint64_t maker_order_id; /* Passive resting order ID */
    uint64_t taker_order_id; /* Aggressive incoming order ID */
    uint64_t price;          /* Execution price */
    uint32_t qty;            /* Executed quantity */
    uint8_t taker_side;      /* LOB_SIDE_BUY or LOB_SIDE_SELL */
    uint8_t _pad[3];
} lob_match_t;

typedef void (*lob_match_callback_t)(const lob_match_t *match, void *user_data);

/* ----------------------------------------------------------------------------
 * Core Data Structures (Cache-Aligned to 64 Bytes)
 * ---------------------------------------------------------------------------- */

typedef struct lob_order lob_order_t;
typedef struct lob_price_level lob_price_level_t;

/*
 * Individual Limit Order (L3). Sized to exactly 64 bytes (1 cache line).
 */
struct LOB_ALIGNED(LOB_CACHE_LINE) lob_order {
    uint64_t order_id;             /* 8 bytes: Unique 64-bit order ID */
    uint64_t price;                /* 8 bytes: Price in ticks */
    uint64_t timestamp;            /* 8 bytes: Sequence / priority timestamp */
    uint32_t qty;                  /* 4 bytes: Open / resting quantity */
    uint32_t filled_qty;           /* 4 bytes: Cumulative filled quantity */
    uint32_t pool_index;           /* 4 bytes: Slot index in pre-allocated pool */
    uint8_t side;                  /* 1 byte:  LOB_SIDE_BUY or LOB_SIDE_SELL */
    uint8_t status;                /* 1 byte:  lob_status_t */
    uint8_t type;                  /* 1 byte:  lob_type_t */
    uint8_t _pad0;                 /* 1 byte:  padding */
    struct lob_order *prev;        /* 8 bytes: Doubly linked list in price level */
    struct lob_order *next;        /* 8 bytes: Doubly linked list next */
    struct lob_price_level *level; /* 8 bytes: Back-pointer to parent price level */
};

/*
 * Aggregated Price Level (L2 node). Sized to exactly 64 bytes (1 cache line).
 */
struct LOB_ALIGNED(LOB_CACHE_LINE) lob_price_level {
    uint64_t price;               /* 8 bytes: Price in ticks */
    uint64_t total_qty;           /* 8 bytes: Aggregated resting quantity */
    uint32_t order_count;         /* 4 bytes: Number of active orders at this price */
    uint32_t pool_index;          /* 4 bytes: Slot index in price level pool */
    uint8_t side;                 /* 1 byte:  LOB_SIDE_BUY or LOB_SIDE_SELL */
    uint8_t _pad0[7];             /* 7 bytes: alignment padding */
    struct lob_order *head;       /* 8 bytes: Oldest order (highest time priority) */
    struct lob_order *tail;       /* 8 bytes: Newest order */
    struct lob_price_level *prev; /* 8 bytes: Doubly linked list of active levels */
    struct lob_price_level *next; /* 8 bytes: Doubly linked list next */
};

/* ----------------------------------------------------------------------------
 * Lock-Free Memory Pool
 * ---------------------------------------------------------------------------- */

typedef struct {
    void *buffer;                              /* Cache-aligned raw storage */
    uint32_t *next_free;                       /* Free list link indices */
    uint32_t capacity;                         /* Total elements capacity */
    LOB_ALIGNED(LOB_CACHE_LINE) uint64_t head; /* Tagged atomic index: (tag << 32) | index */
} lob_pool_t;

/* ----------------------------------------------------------------------------
 * Fast Open-Addressing Hash Maps
 * ---------------------------------------------------------------------------- */

typedef struct {
    uint64_t key;
    uint32_t val;
    uint32_t _pad;
} lob_map_slot_t;

typedef struct {
    lob_map_slot_t *slots;
    size_t capacity;
    size_t mask;
    size_t count;
} lob_map_t;

/* ----------------------------------------------------------------------------
 * Limit Order Book (L3 Engine State)
 * ---------------------------------------------------------------------------- */

typedef struct lob_book {
    lob_price_level_t *bids_head; /* Best bid (highest price level) */
    lob_price_level_t *asks_head; /* Best ask (lowest price level) */

    lob_pool_t order_pool; /* Lock-free pool for order nodes */
    lob_pool_t level_pool; /* Lock-free pool for price level nodes */

    lob_map_t order_map;     /* order_id -> order pool index (O(1)) */
    lob_map_t bid_price_map; /* price -> bid price level pool index (O(1)) */
    lob_map_t ask_price_map; /* price -> ask price level pool index (O(1)) */

    uint64_t sequence;     /* Monotonic order timestamp counter */
    uint64_t total_volume; /* Cumulative matched trade volume */
    uint64_t trade_count;  /* Cumulative matched trade count */
    size_t active_orders;  /* Number of resting orders currently in book */
} lob_book_t;

/* ----------------------------------------------------------------------------
 * Lock-Free Command Queue (MPMC Bounded Ring Buffer)
 * ---------------------------------------------------------------------------- */

typedef struct {
    uint8_t type;       /* lob_cmd_type_t */
    uint8_t side;       /* lob_side_t */
    uint8_t order_type; /* lob_type_t */
    uint8_t _pad;
    uint32_t qty;      /* Order quantity */
    uint64_t order_id; /* Order identifier */
    uint64_t price;    /* Order price */
} lob_command_t;

typedef struct {
    LOB_ALIGNED(LOB_CACHE_LINE) uint64_t sequence;
    lob_command_t cmd;
} lob_queue_slot_t;

typedef struct lob_queue {
    LOB_ALIGNED(LOB_CACHE_LINE) uint64_t enqueue_pos;
    uint8_t _pad0[LOB_CACHE_LINE - sizeof(uint64_t)];
    LOB_ALIGNED(LOB_CACHE_LINE) uint64_t dequeue_pos;
    uint8_t _pad1[LOB_CACHE_LINE - sizeof(uint64_t)];
    size_t capacity;
    size_t mask;
    lob_queue_slot_t *buffer;
} lob_queue_t;

/* ----------------------------------------------------------------------------
 * Order Book Lifecycle & Management
 * ---------------------------------------------------------------------------- */

/*
 * Allocate and initialize a new limit order book.
 * max_orders: maximum active resting orders (must be > 0).
 * max_levels: maximum active price levels (must be > 0).
 */
lob_book_t *lob_create(size_t max_orders, size_t max_levels);

/*
 * Release all book resources, pools, and maps. Safe with NULL.
 */
void lob_free(lob_book_t *book);

/*
 * Clear all resting orders and reset book state to empty.
 */
void lob_clear(lob_book_t *book);

/* ----------------------------------------------------------------------------
 * Order Operations (L3 MBO)
 * ---------------------------------------------------------------------------- */

/*
 * Submit an order into the order book.
 * If order crosses opposing liquidity, matches in FIFO price-time priority.
 * Any unfilled portion rests on the book (unless IOC/FOK/Market).
 * on_match: optional callback invoked for each generated fill (can be NULL).
 */
lob_result_t lob_add_order(lob_book_t *book, uint64_t order_id, lob_side_t side, uint64_t price,
                           uint32_t qty, lob_type_t type, lob_match_callback_t on_match,
                           void *user_data);

/*
 * Cancel an active order by order_id in O(1).
 */
lob_result_t lob_cancel_order(lob_book_t *book, uint64_t order_id);

/*
 * Modify quantity or price of an existing order.
 * Reducing quantity at same price retains time priority.
 * Increasing quantity or changing price moves order to tail of new level.
 */
lob_result_t lob_modify_order(lob_book_t *book, uint64_t order_id, uint32_t new_qty,
                              uint64_t new_price, lob_match_callback_t on_match, void *user_data);

/*
 * Lookup an active order in O(1). Returns pointer to order or NULL if not found.
 */
const lob_order_t *lob_find_order(const lob_book_t *book, uint64_t order_id);

/* ----------------------------------------------------------------------------
 * Market Data Queries (L1 & L2)
 * ---------------------------------------------------------------------------- */

/*
 * Retrieve Best Bid and Best Ask (BBO). Returns true if at least one side exists.
 */
bool lob_get_bbo(const lob_book_t *book, uint64_t *best_bid_price, uint32_t *best_bid_qty,
                 uint64_t *best_ask_price, uint32_t *best_ask_qty);

/*
 * Returns current bid-ask spread in ticks, or UINT64_MAX if two-sided market absent.
 */
uint64_t lob_get_spread(const lob_book_t *book);

/*
 * Returns mid price in ticks ((best_bid + best_ask) / 2), or 0 if absent.
 */
uint64_t lob_get_mid_price(const lob_book_t *book);

/*
 * Extract top N price levels for market depth (L2 snapshot).
 * Returns the number of levels populated.
 */
size_t lob_get_depth(const lob_book_t *book, lob_side_t side, size_t max_levels,
                     uint64_t *prices_out, uint64_t *quantities_out, uint32_t *order_counts_out);

/*
 * Query active counts.
 */
size_t lob_order_count(const lob_book_t *book);
size_t lob_level_count(const lob_book_t *book, lob_side_t side);
uint64_t lob_total_volume(const lob_book_t *book);
uint64_t lob_trade_count(const lob_book_t *book);

/* ----------------------------------------------------------------------------
 * Lock-Free Command Queue (Disruptor / Gateway Pattern)
 * ---------------------------------------------------------------------------- */

/*
 * Create a lock-free MPMC ring buffer. Capacity must be a power of two.
 */
lob_queue_t *lob_queue_create(size_t capacity);

/*
 * Free a lock-free queue.
 */
void lob_queue_free(lob_queue_t *queue);

/*
 * Enqueue order command lock-free (thread-safe). Returns false if full.
 */
bool lob_queue_push(lob_queue_t *queue, const lob_command_t *cmd);

/*
 * Dequeue order command lock-free (thread-safe). Returns false if empty.
 */
bool lob_queue_pop(lob_queue_t *queue, lob_command_t *cmd);

/*
 * Batch-process pending commands from queue into order book.
 * max_cmds: maximum commands to process (0 = all currently pending).
 * Returns number of commands processed.
 */
size_t lob_process_queue(lob_book_t *book, lob_queue_t *queue, size_t max_cmds,
                         lob_match_callback_t on_match, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* ORDER_BOOK_H */
