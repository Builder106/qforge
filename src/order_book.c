/* ============================================================================
 * order_book.c — Lock-Free Cache-Aligned L3 Limit Order Book Implementation
 * ============================================================================ */

#include "order_book.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LOB_MAP_EMPTY UINT32_MAX
#define LOB_POOL_EMPTY 0xFFFFFFFFU

/* ----------------------------------------------------------------------------
 * Portable Cache-Aligned Memory Management
 * ---------------------------------------------------------------------------- */

static void *lob_aligned_alloc(size_t alignment, size_t size) {
    if (alignment < sizeof(void *)) {
        alignment = sizeof(void *);
    }
    void *raw = malloc(size + alignment + sizeof(void *));
    if (!raw) {
        return NULL;
    }
    char *aligned =
        (char *)(((uintptr_t)raw + sizeof(void *) + alignment - 1) & ~(uintptr_t)(alignment - 1));
    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

static void lob_aligned_free(void *ptr) {
    if (ptr) {
        void *raw = ((void **)ptr)[-1];
        free(raw);
    }
}

/* ----------------------------------------------------------------------------
 * Fast 64-bit Integer Hash Function (SplitMix64)
 * ---------------------------------------------------------------------------- */

static inline uint64_t lob_hash_u64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static size_t lob_next_power_of_two(size_t n) {
    if (n < 2) {
        n = 2;
    }
    size_t p = 1;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

/* ----------------------------------------------------------------------------
 * Fast Open-Addressing Hash Map (Backward-Shift Deletion)
 * ---------------------------------------------------------------------------- */

static bool lob_map_init(lob_map_t *m, size_t min_capacity) {
    size_t cap = lob_next_power_of_two(min_capacity * 2);
    m->capacity = cap;
    m->mask = cap - 1;
    m->count = 0;
    m->slots = (lob_map_slot_t *)malloc(cap * sizeof(lob_map_slot_t));
    if (!m->slots) {
        return false;
    }
    for (size_t i = 0; i < cap; i++) {
        m->slots[i].val = LOB_MAP_EMPTY;
        m->slots[i].key = 0;
    }
    return true;
}

static void lob_map_destroy(lob_map_t *m) {
    if (m && m->slots) {
        free(m->slots);
        m->slots = NULL;
        m->capacity = 0;
        m->count = 0;
    }
}

static void lob_map_clear(lob_map_t *m) {
    if (m && m->slots) {
        for (size_t i = 0; i < m->capacity; i++) {
            m->slots[i].val = LOB_MAP_EMPTY;
            m->slots[i].key = 0;
        }
        m->count = 0;
    }
}

static bool lob_map_put(lob_map_t *m, uint64_t key, uint32_t val) {
    if (m->count >= (m->capacity * 7) / 10) {
        return false;
    }
    size_t idx = (size_t)(lob_hash_u64(key) & m->mask);
    while (m->slots[idx].val != LOB_MAP_EMPTY) {
        if (m->slots[idx].key == key) {
            m->slots[idx].val = val;
            return true;
        }
        idx = (idx + 1) & m->mask;
    }
    m->slots[idx].key = key;
    m->slots[idx].val = val;
    m->count++;
    return true;
}

static uint32_t lob_map_get(const lob_map_t *m, uint64_t key) {
    if (!m || !m->slots || m->count == 0) {
        return LOB_MAP_EMPTY;
    }
    size_t idx = (size_t)(lob_hash_u64(key) & m->mask);
    while (m->slots[idx].val != LOB_MAP_EMPTY) {
        if (m->slots[idx].key == key) {
            return m->slots[idx].val;
        }
        idx = (idx + 1) & m->mask;
    }
    return LOB_MAP_EMPTY;
}

static bool lob_map_remove(lob_map_t *m, uint64_t key) {
    if (!m || !m->slots || m->count == 0) {
        return false;
    }
    size_t idx = (size_t)(lob_hash_u64(key) & m->mask);
    while (m->slots[idx].val != LOB_MAP_EMPTY) {
        if (m->slots[idx].key == key) {
            size_t curr = idx;
            size_t next = (curr + 1) & m->mask;
            while (m->slots[next].val != LOB_MAP_EMPTY) {
                size_t natural = (size_t)(lob_hash_u64(m->slots[next].key) & m->mask);
                bool can_shift = false;
                if (curr <= next) {
                    can_shift = (natural <= curr || natural > next);
                } else {
                    can_shift = (natural <= curr && natural > next);
                }
                if (can_shift) {
                    m->slots[curr] = m->slots[next];
                    curr = next;
                }
                next = (next + 1) & m->mask;
            }
            m->slots[curr].val = LOB_MAP_EMPTY;
            m->slots[curr].key = 0;
            m->count--;
            return true;
        }
        idx = (idx + 1) & m->mask;
    }
    return false;
}

/* ----------------------------------------------------------------------------
 * Lock-Free Memory Pool (Tagged Index Treiber Stack)
 * ---------------------------------------------------------------------------- */

static bool lob_pool_init(lob_pool_t *pool, size_t elem_size, uint32_t capacity) {
    pool->capacity = capacity;
    pool->buffer = lob_aligned_alloc(LOB_CACHE_LINE, elem_size * capacity);
    if (!pool->buffer) {
        return false;
    }
    memset(pool->buffer, 0, elem_size * capacity);

    pool->next_free = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    if (!pool->next_free) {
        lob_aligned_free(pool->buffer);
        pool->buffer = NULL;
        return false;
    }

    for (uint32_t i = 0; i < capacity; i++) {
        pool->next_free[i] = (i + 1 < capacity) ? (i + 1) : LOB_POOL_EMPTY;
    }
    pool->head = 0; /* Tag 0, index 0 */
    return true;
}

static void lob_pool_destroy(lob_pool_t *pool) {
    if (pool) {
        if (pool->buffer) {
            lob_aligned_free(pool->buffer);
            pool->buffer = NULL;
        }
        if (pool->next_free) {
            free(pool->next_free);
            pool->next_free = NULL;
        }
        pool->capacity = 0;
        pool->head = 0;
    }
}

static void lob_pool_reset(lob_pool_t *pool) {
    if (pool && pool->next_free) {
        for (uint32_t i = 0; i < pool->capacity; i++) {
            pool->next_free[i] = (i + 1 < pool->capacity) ? (i + 1) : LOB_POOL_EMPTY;
        }
        pool->head = 0;
    }
}

static uint32_t lob_pool_pop(lob_pool_t *pool) {
    uint64_t current = __atomic_load_n(&pool->head, __ATOMIC_ACQUIRE);
    while (1) {
        uint32_t idx = (uint32_t)(current & 0xFFFFFFFFULL);
        if (idx == LOB_POOL_EMPTY) {
            return LOB_POOL_EMPTY;
        }
        uint32_t tag = (uint32_t)(current >> 32);
        uint32_t next_idx = pool->next_free[idx];
        uint64_t next_head = (((uint64_t)(tag + 1)) << 32) | (uint64_t)next_idx;
        if (__atomic_compare_exchange_n(&pool->head, &current, next_head, 1, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return idx;
        }
    }
}

static void lob_pool_push(lob_pool_t *pool, uint32_t idx) {
    if (idx >= pool->capacity) {
        return;
    }
    uint64_t current = __atomic_load_n(&pool->head, __ATOMIC_ACQUIRE);
    while (1) {
        uint32_t head_idx = (uint32_t)(current & 0xFFFFFFFFULL);
        pool->next_free[idx] = head_idx;
        uint32_t tag = (uint32_t)(current >> 32);
        uint64_t next_head = (((uint64_t)(tag + 1)) << 32) | (uint64_t)idx;
        if (__atomic_compare_exchange_n(&pool->head, &current, next_head, 1, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return;
        }
    }
}

static inline lob_order_t *lob_pool_order_at(lob_pool_t *pool, uint32_t idx) {
    return &((lob_order_t *)pool->buffer)[idx];
}

static inline lob_price_level_t *lob_pool_level_at(lob_pool_t *pool, uint32_t idx) {
    return &((lob_price_level_t *)pool->buffer)[idx];
}

/* ----------------------------------------------------------------------------
 * Order Book Lifecycle
 * ---------------------------------------------------------------------------- */

lob_book_t *lob_create(size_t max_orders, size_t max_levels) {
    if (max_orders == 0 || max_levels == 0) {
        return NULL;
    }
    lob_book_t *book = (lob_book_t *)lob_aligned_alloc(LOB_CACHE_LINE, sizeof(lob_book_t));
    if (!book) {
        return NULL;
    }
    memset(book, 0, sizeof(lob_book_t));

    if (!lob_pool_init(&book->order_pool, sizeof(lob_order_t), (uint32_t)max_orders)) {
        lob_free(book);
        return NULL;
    }

    if (!lob_pool_init(&book->level_pool, sizeof(lob_price_level_t), (uint32_t)max_levels)) {
        lob_free(book);
        return NULL;
    }

    if (!lob_map_init(&book->order_map, max_orders) ||
        !lob_map_init(&book->bid_price_map, max_levels) ||
        !lob_map_init(&book->ask_price_map, max_levels)) {
        lob_free(book);
        return NULL;
    }

    book->bids_head = NULL;
    book->asks_head = NULL;
    book->sequence = 0;
    book->total_volume = 0;
    book->trade_count = 0;
    book->active_orders = 0;

    return book;
}

void lob_free(lob_book_t *book) {
    if (!book) {
        return;
    }
    lob_pool_destroy(&book->order_pool);
    lob_pool_destroy(&book->level_pool);
    lob_map_destroy(&book->order_map);
    lob_map_destroy(&book->bid_price_map);
    lob_map_destroy(&book->ask_price_map);
    lob_aligned_free(book);
}

void lob_clear(lob_book_t *book) {
    if (!book) {
        return;
    }
    book->bids_head = NULL;
    book->asks_head = NULL;
    book->active_orders = 0;
    book->sequence = 0;
    book->total_volume = 0;
    book->trade_count = 0;

    lob_pool_reset(&book->order_pool);
    lob_pool_reset(&book->level_pool);
    lob_map_clear(&book->order_map);
    lob_map_clear(&book->bid_price_map);
    lob_map_clear(&book->ask_price_map);
}

/* ----------------------------------------------------------------------------
 * Internal Price Level Helpers
 * ---------------------------------------------------------------------------- */

static void lob_insert_bid_level(lob_book_t *book, lob_price_level_t *new_level) {
    /* Bids sorted descending: highest price first */
    if (!book->bids_head || new_level->price > book->bids_head->price) {
        new_level->prev = NULL;
        new_level->next = book->bids_head;
        if (book->bids_head) {
            book->bids_head->prev = new_level;
        }
        book->bids_head = new_level;
        return;
    }

    lob_price_level_t *curr = book->bids_head;
    while (curr->next && curr->next->price > new_level->price) {
        curr = curr->next;
    }

    new_level->prev = curr;
    new_level->next = curr->next;
    if (curr->next) {
        curr->next->prev = new_level;
    }
    curr->next = new_level;
}

static void lob_insert_ask_level(lob_book_t *book, lob_price_level_t *new_level) {
    /* Asks sorted ascending: lowest price first */
    if (!book->asks_head || new_level->price < book->asks_head->price) {
        new_level->prev = NULL;
        new_level->next = book->asks_head;
        if (book->asks_head) {
            book->asks_head->prev = new_level;
        }
        book->asks_head = new_level;
        return;
    }

    lob_price_level_t *curr = book->asks_head;
    while (curr->next && curr->next->price < new_level->price) {
        curr = curr->next;
    }

    new_level->prev = curr;
    new_level->next = curr->next;
    if (curr->next) {
        curr->next->prev = new_level;
    }
    curr->next = new_level;
}

static void lob_remove_level(lob_book_t *book, lob_price_level_t *level) {
    if (level->prev) {
        level->prev->next = level->next;
    } else {
        if (level->side == LOB_SIDE_BUY) {
            book->bids_head = level->next;
        } else {
            book->asks_head = level->next;
        }
    }

    if (level->next) {
        level->next->prev = level->prev;
    }

    if (level->side == LOB_SIDE_BUY) {
        lob_map_remove(&book->bid_price_map, level->price);
    } else {
        lob_map_remove(&book->ask_price_map, level->price);
    }

    uint32_t pidx = level->pool_index;
    memset(level, 0, sizeof(lob_price_level_t));
    lob_pool_push(&book->level_pool, pidx);
}

/* ----------------------------------------------------------------------------
 * Order Operations
 * ---------------------------------------------------------------------------- */

lob_result_t lob_add_order(lob_book_t *book, uint64_t order_id, lob_side_t side, uint64_t price,
                           uint32_t qty, lob_type_t type, lob_match_callback_t on_match,
                           void *user_data) {
    if (!book || order_id == 0 || qty == 0) {
        return LOB_RESULT_INVALID_ARG;
    }
    if (type != LOB_TYPE_MARKET && price == 0) {
        return LOB_RESULT_INVALID_ARG;
    }
    if (lob_map_get(&book->order_map, order_id) != LOB_MAP_EMPTY) {
        return LOB_RESULT_DUPLICATE_ORDER_ID;
    }

    /* Post-Only check: must not match immediately */
    if (type == LOB_TYPE_POST_ONLY) {
        if (side == LOB_SIDE_BUY && book->asks_head && price >= book->asks_head->price) {
            return LOB_RESULT_POST_ONLY_CROSSED;
        }
        if (side == LOB_SIDE_SELL && book->bids_head && price <= book->bids_head->price) {
            return LOB_RESULT_POST_ONLY_CROSSED;
        }
    }

    /* Fill-Or-Kill (FOK) pre-check: verify entire quantity can be filled */
    if (type == LOB_TYPE_FOK) {
        uint64_t fillable = 0;
        if (side == LOB_SIDE_BUY) {
            lob_price_level_t *lvl = book->asks_head;
            while (lvl && lvl->price <= price) {
                fillable += lvl->total_qty;
                if (fillable >= qty) {
                    break;
                }
                lvl = lvl->next;
            }
        } else {
            lob_price_level_t *lvl = book->bids_head;
            while (lvl && lvl->price >= price) {
                fillable += lvl->total_qty;
                if (fillable >= qty) {
                    break;
                }
                lvl = lvl->next;
            }
        }
        if (fillable < qty) {
            return LOB_RESULT_FOK_NOT_FILLED;
        }
    }

    uint32_t remaining_qty = qty;

    /* ---- Matching Path ---- */
    if (side == LOB_SIDE_BUY) {
        while (remaining_qty > 0 && book->asks_head) {
            lob_price_level_t *ask_lvl = book->asks_head;
            if (type != LOB_TYPE_MARKET && price < ask_lvl->price) {
                break; /* Price does not cross */
            }

            lob_order_t *maker = ask_lvl->head;
            while (maker && remaining_qty > 0) {
                uint32_t match_qty = (remaining_qty < maker->qty) ? remaining_qty : maker->qty;
                remaining_qty -= match_qty;
                maker->qty -= match_qty;
                maker->filled_qty += match_qty;
                ask_lvl->total_qty -= match_qty;
                book->total_volume += match_qty;
                book->trade_count++;

                if (on_match) {
                    lob_match_t match;
                    match.maker_order_id = maker->order_id;
                    match.taker_order_id = order_id;
                    match.price = ask_lvl->price;
                    match.qty = match_qty;
                    match.taker_side = LOB_SIDE_BUY;
                    on_match(&match, user_data);
                }

                if (maker->qty == 0) {
                    /* Maker order completely filled */
                    maker->status = LOB_STATUS_FILLED;
                    lob_order_t *next_maker = maker->next;

                    ask_lvl->head = next_maker;
                    if (next_maker) {
                        next_maker->prev = NULL;
                    } else {
                        ask_lvl->tail = NULL;
                    }
                    ask_lvl->order_count--;
                    book->active_orders--;

                    lob_map_remove(&book->order_map, maker->order_id);
                    uint32_t midx = maker->pool_index;
                    memset(maker, 0, sizeof(lob_order_t));
                    lob_pool_push(&book->order_pool, midx);

                    maker = next_maker;
                }
            }

            if (ask_lvl->order_count == 0) {
                lob_remove_level(book, ask_lvl);
            }
        }
    } else {
        /* Sell order matching against resting bids */
        while (remaining_qty > 0 && book->bids_head) {
            lob_price_level_t *bid_lvl = book->bids_head;
            if (type != LOB_TYPE_MARKET && price > bid_lvl->price) {
                break; /* Price does not cross */
            }

            lob_order_t *maker = bid_lvl->head;
            while (maker && remaining_qty > 0) {
                uint32_t match_qty = (remaining_qty < maker->qty) ? remaining_qty : maker->qty;
                remaining_qty -= match_qty;
                maker->qty -= match_qty;
                maker->filled_qty += match_qty;
                bid_lvl->total_qty -= match_qty;
                book->total_volume += match_qty;
                book->trade_count++;

                if (on_match) {
                    lob_match_t match;
                    match.maker_order_id = maker->order_id;
                    match.taker_order_id = order_id;
                    match.price = bid_lvl->price;
                    match.qty = match_qty;
                    match.taker_side = LOB_SIDE_SELL;
                    on_match(&match, user_data);
                }

                if (maker->qty == 0) {
                    maker->status = LOB_STATUS_FILLED;
                    lob_order_t *next_maker = maker->next;

                    bid_lvl->head = next_maker;
                    if (next_maker) {
                        next_maker->prev = NULL;
                    } else {
                        bid_lvl->tail = NULL;
                    }
                    bid_lvl->order_count--;
                    book->active_orders--;

                    lob_map_remove(&book->order_map, maker->order_id);
                    uint32_t midx = maker->pool_index;
                    memset(maker, 0, sizeof(lob_order_t));
                    lob_pool_push(&book->order_pool, midx);

                    maker = next_maker;
                }
            }

            if (bid_lvl->order_count == 0) {
                lob_remove_level(book, bid_lvl);
            }
        }
    }

    /* ---- Resting Placement ---- */
    if (remaining_qty > 0 && type != LOB_TYPE_IOC && type != LOB_TYPE_MARKET) {
        uint32_t oidx = lob_pool_pop(&book->order_pool);
        if (oidx == LOB_POOL_EMPTY) {
            return LOB_RESULT_POOL_EXHAUSTED;
        }

        /* Check or allocate price level */
        lob_map_t *price_map = (side == LOB_SIDE_BUY) ? &book->bid_price_map : &book->ask_price_map;
        uint32_t lidx = lob_map_get(price_map, price);
        lob_price_level_t *level = NULL;

        if (lidx == LOB_MAP_EMPTY) {
            lidx = lob_pool_pop(&book->level_pool);
            if (lidx == LOB_POOL_EMPTY) {
                lob_pool_push(&book->order_pool, oidx);
                return LOB_RESULT_POOL_EXHAUSTED;
            }
            level = lob_pool_level_at(&book->level_pool, lidx);
            memset(level, 0, sizeof(lob_price_level_t));
            level->price = price;
            level->side = (uint8_t)side;
            level->pool_index = lidx;

            if (side == LOB_SIDE_BUY) {
                lob_insert_bid_level(book, level);
            } else {
                lob_insert_ask_level(book, level);
            }
            lob_map_put(price_map, price, lidx);
        } else {
            level = lob_pool_level_at(&book->level_pool, lidx);
        }

        /* Populate order */
        lob_order_t *ord = lob_pool_order_at(&book->order_pool, oidx);
        memset(ord, 0, sizeof(lob_order_t));
        ord->order_id = order_id;
        ord->price = price;
        ord->qty = remaining_qty;
        ord->filled_qty = qty - remaining_qty;
        ord->side = (uint8_t)side;
        ord->status = LOB_STATUS_ACTIVE;
        ord->type = (uint8_t)type;
        ord->timestamp = ++book->sequence;
        ord->pool_index = oidx;
        ord->level = level;

        /* Append order to tail of level (time priority FIFO) */
        ord->prev = level->tail;
        ord->next = NULL;
        if (level->tail) {
            level->tail->next = ord;
        } else {
            level->head = ord;
        }
        level->tail = ord;
        level->total_qty += remaining_qty;
        level->order_count++;

        lob_map_put(&book->order_map, order_id, oidx);
        book->active_orders++;
    }

    return LOB_RESULT_OK;
}

lob_result_t lob_cancel_order(lob_book_t *book, uint64_t order_id) {
    if (!book || order_id == 0) {
        return LOB_RESULT_INVALID_ARG;
    }
    uint32_t oidx = lob_map_get(&book->order_map, order_id);
    if (oidx == LOB_MAP_EMPTY) {
        return LOB_RESULT_ORDER_NOT_FOUND;
    }

    lob_order_t *ord = lob_pool_order_at(&book->order_pool, oidx);
    lob_price_level_t *level = ord->level;

    /* Unlink order from price level */
    if (ord->prev) {
        ord->prev->next = ord->next;
    } else {
        level->head = ord->next;
    }

    if (ord->next) {
        ord->next->prev = ord->prev;
    } else {
        level->tail = ord->prev;
    }

    level->total_qty -= ord->qty;
    level->order_count--;
    book->active_orders--;

    if (level->order_count == 0) {
        lob_remove_level(book, level);
    }

    lob_map_remove(&book->order_map, order_id);
    ord->status = LOB_STATUS_CANCELLED;
    memset(ord, 0, sizeof(lob_order_t));
    lob_pool_push(&book->order_pool, oidx);

    return LOB_RESULT_OK;
}

lob_result_t lob_modify_order(lob_book_t *book, uint64_t order_id, uint32_t new_qty,
                              uint64_t new_price, lob_match_callback_t on_match, void *user_data) {
    if (!book || order_id == 0) {
        return LOB_RESULT_INVALID_ARG;
    }
    if (new_qty == 0) {
        return lob_cancel_order(book, order_id);
    }

    uint32_t oidx = lob_map_get(&book->order_map, order_id);
    if (oidx == LOB_MAP_EMPTY) {
        return LOB_RESULT_ORDER_NOT_FOUND;
    }

    lob_order_t *ord = lob_pool_order_at(&book->order_pool, oidx);
    uint64_t target_price = (new_price > 0) ? new_price : ord->price;

    /* If price is identical and quantity is reduced, retain time priority */
    if (target_price == ord->price && new_qty <= ord->qty) {
        uint32_t delta = ord->qty - new_qty;
        ord->qty = new_qty;
        ord->level->total_qty -= delta;
        return LOB_RESULT_OK;
    }

    /* Increasing quantity or changing price loses priority: cancel and re-enter */
    lob_side_t side = (lob_side_t)ord->side;
    lob_type_t type = (lob_type_t)ord->type;

    lob_result_t res = lob_cancel_order(book, order_id);
    if (res != LOB_RESULT_OK) {
        return res;
    }

    return lob_add_order(book, order_id, side, target_price, new_qty, type, on_match, user_data);
}

const lob_order_t *lob_find_order(const lob_book_t *book, uint64_t order_id) {
    if (!book || order_id == 0) {
        return NULL;
    }
    uint32_t oidx = lob_map_get(&book->order_map, order_id);
    if (oidx == LOB_MAP_EMPTY) {
        return NULL;
    }
    return (const lob_order_t *)lob_pool_order_at((lob_pool_t *)&book->order_pool, oidx);
}

/* ----------------------------------------------------------------------------
 * Market Data Queries
 * ---------------------------------------------------------------------------- */

bool lob_get_bbo(const lob_book_t *book, uint64_t *best_bid_price, uint32_t *best_bid_qty,
                 uint64_t *best_ask_price, uint32_t *best_ask_qty) {
    if (!book) {
        return false;
    }

    bool has_bid = (book->bids_head != NULL);
    bool has_ask = (book->asks_head != NULL);

    if (best_bid_price) {
        *best_bid_price = has_bid ? book->bids_head->price : 0;
    }
    if (best_bid_qty) {
        *best_bid_qty = has_bid ? (uint32_t)book->bids_head->total_qty : 0;
    }
    if (best_ask_price) {
        *best_ask_price = has_ask ? book->asks_head->price : 0;
    }
    if (best_ask_qty) {
        *best_ask_qty = has_ask ? (uint32_t)book->asks_head->total_qty : 0;
    }

    return (has_bid || has_ask);
}

uint64_t lob_get_spread(const lob_book_t *book) {
    if (!book || !book->bids_head || !book->asks_head) {
        return UINT64_MAX;
    }
    if (book->asks_head->price <= book->bids_head->price) {
        return 0;
    }
    return book->asks_head->price - book->bids_head->price;
}

uint64_t lob_get_mid_price(const lob_book_t *book) {
    if (!book || !book->bids_head || !book->asks_head) {
        return 0;
    }
    return (book->bids_head->price + book->asks_head->price) / 2;
}

size_t lob_get_depth(const lob_book_t *book, lob_side_t side, size_t max_levels,
                     uint64_t *prices_out, uint64_t *quantities_out, uint32_t *order_counts_out) {
    if (!book || max_levels == 0) {
        return 0;
    }

    size_t count = 0;
    const lob_price_level_t *lvl = (side == LOB_SIDE_BUY) ? book->bids_head : book->asks_head;

    while (lvl && count < max_levels) {
        if (prices_out) {
            prices_out[count] = lvl->price;
        }
        if (quantities_out) {
            quantities_out[count] = lvl->total_qty;
        }
        if (order_counts_out) {
            order_counts_out[count] = lvl->order_count;
        }
        count++;
        lvl = lvl->next;
    }

    return count;
}

size_t lob_order_count(const lob_book_t *book) {
    return book ? book->active_orders : 0;
}

size_t lob_level_count(const lob_book_t *book, lob_side_t side) {
    if (!book) {
        return 0;
    }
    size_t count = 0;
    const lob_price_level_t *lvl = (side == LOB_SIDE_BUY) ? book->bids_head : book->asks_head;
    while (lvl) {
        count++;
        lvl = lvl->next;
    }
    return count;
}

uint64_t lob_total_volume(const lob_book_t *book) {
    return book ? book->total_volume : 0;
}

uint64_t lob_trade_count(const lob_book_t *book) {
    return book ? book->trade_count : 0;
}

/* ----------------------------------------------------------------------------
 * Lock-Free MPMC Command Queue Implementation (Dmitry Vyukov Algorithm)
 * ---------------------------------------------------------------------------- */

lob_queue_t *lob_queue_create(size_t capacity) {
    size_t cap = lob_next_power_of_two(capacity);
    lob_queue_t *q = (lob_queue_t *)lob_aligned_alloc(LOB_CACHE_LINE, sizeof(lob_queue_t));
    if (!q) {
        return NULL;
    }
    memset(q, 0, sizeof(lob_queue_t));

    q->capacity = cap;
    q->mask = cap - 1;
    q->enqueue_pos = 0;
    q->dequeue_pos = 0;

    q->buffer =
        (lob_queue_slot_t *)lob_aligned_alloc(LOB_CACHE_LINE, cap * sizeof(lob_queue_slot_t));
    if (!q->buffer) {
        lob_aligned_free(q);
        return NULL;
    }

    for (size_t i = 0; i < cap; i++) {
        q->buffer[i].sequence = i;
        memset(&q->buffer[i].cmd, 0, sizeof(lob_command_t));
    }

    return q;
}

void lob_queue_free(lob_queue_t *queue) {
    if (queue) {
        if (queue->buffer) {
            lob_aligned_free(queue->buffer);
            queue->buffer = NULL;
        }
        lob_aligned_free(queue);
    }
}

bool lob_queue_push(lob_queue_t *queue, const lob_command_t *cmd) {
    if (!queue || !cmd) {
        return false;
    }
    uint64_t pos = __atomic_load_n(&queue->enqueue_pos, __ATOMIC_RELAXED);
    while (1) {
        lob_queue_slot_t *slot = &queue->buffer[pos & queue->mask];
        uint64_t seq = __atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE);
        int64_t diff = (int64_t)seq - (int64_t)pos;
        if (diff == 0) {
            if (__atomic_compare_exchange_n(&queue->enqueue_pos, &pos, pos + 1, 1, __ATOMIC_RELAXED,
                                            __ATOMIC_RELAXED)) {
                slot->cmd = *cmd;
                __atomic_store_n(&slot->sequence, pos + 1, __ATOMIC_RELEASE);
                return true;
            }
        } else if (diff < 0) {
            return false; /* Queue full */
        } else {
            pos = __atomic_load_n(&queue->enqueue_pos, __ATOMIC_RELAXED);
        }
    }
}

bool lob_queue_pop(lob_queue_t *queue, lob_command_t *cmd) {
    if (!queue || !cmd) {
        return false;
    }
    uint64_t pos = __atomic_load_n(&queue->dequeue_pos, __ATOMIC_RELAXED);
    while (1) {
        lob_queue_slot_t *slot = &queue->buffer[pos & queue->mask];
        uint64_t seq = __atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE);
        int64_t diff = (int64_t)seq - (int64_t)(pos + 1);
        if (diff == 0) {
            if (__atomic_compare_exchange_n(&queue->dequeue_pos, &pos, pos + 1, 1, __ATOMIC_RELAXED,
                                            __ATOMIC_RELAXED)) {
                *cmd = slot->cmd;
                __atomic_store_n(&slot->sequence, pos + queue->mask + 1, __ATOMIC_RELEASE);
                return true;
            }
        } else if (diff < 0) {
            return false; /* Queue empty */
        } else {
            pos = __atomic_load_n(&queue->dequeue_pos, __ATOMIC_RELAXED);
        }
    }
}

size_t lob_process_queue(lob_book_t *book, lob_queue_t *queue, size_t max_cmds,
                         lob_match_callback_t on_match, void *user_data) {
    if (!book || !queue) {
        return 0;
    }
    size_t processed = 0;
    lob_command_t cmd;

    while (max_cmds == 0 || processed < max_cmds) {
        if (!lob_queue_pop(queue, &cmd)) {
            break;
        }
        switch (cmd.type) {
            case LOB_CMD_ADD:
                lob_add_order(book, cmd.order_id, (lob_side_t)cmd.side, cmd.price, cmd.qty,
                              (lob_type_t)cmd.order_type, on_match, user_data);
                break;
            case LOB_CMD_CANCEL:
                lob_cancel_order(book, cmd.order_id);
                break;
            case LOB_CMD_MODIFY:
                lob_modify_order(book, cmd.order_id, cmd.qty, cmd.price, on_match, user_data);
                break;
            default:
                break;
        }
        processed++;
    }

    return processed;
}
