/* ============================================================================
 * order_book.hpp — C++17 Modern Wrapper for Lock-Free L3 Limit Order Book
 *
 * Provides a type-safe, idiomatic C++ interface for the QForge L3 Limit Order
 * Book engine, with RAII resource management, std::optional, std::vector,
 * and strongly typed enums.
 * ============================================================================ */

#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include "order_book.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace qforge {

enum class Side : uint8_t {
    Buy = LOB_SIDE_BUY,
    Sell = LOB_SIDE_SELL
};

enum class OrderStatus : uint8_t {
    Active = LOB_STATUS_ACTIVE,
    Filled = LOB_STATUS_FILLED,
    Cancelled = LOB_STATUS_CANCELLED,
    Rejected = LOB_STATUS_REJECTED
};

enum class OrderType : uint8_t {
    Limit = LOB_TYPE_LIMIT,
    IOC = LOB_TYPE_IOC,
    FOK = LOB_TYPE_FOK,
    PostOnly = LOB_TYPE_POST_ONLY,
    Market = LOB_TYPE_MARKET
};

enum class Result : int {
    Ok = LOB_RESULT_OK,
    InvalidArg = LOB_RESULT_INVALID_ARG,
    DuplicateOrderId = LOB_RESULT_DUPLICATE_ORDER_ID,
    OrderNotFound = LOB_RESULT_ORDER_NOT_FOUND,
    PoolExhausted = LOB_RESULT_POOL_EXHAUSTED,
    PostOnlyCrossed = LOB_RESULT_POST_ONLY_CROSSED,
    FokNotFilled = LOB_RESULT_FOK_NOT_FILLED
};

struct Match {
    uint64_t maker_order_id{0};
    uint64_t taker_order_id{0};
    uint64_t price{0};
    uint32_t qty{0};
    Side taker_side{Side::Buy};
};

struct Level {
    uint64_t price{0};
    uint64_t total_qty{0};
    uint32_t order_count{0};
};

struct OrderInfo {
    uint64_t order_id{0};
    uint64_t price{0};
    uint64_t timestamp{0};
    uint32_t qty{0};
    uint32_t filled_qty{0};
    Side side{Side::Buy};
    OrderStatus status{OrderStatus::Active};
    OrderType type{OrderType::Limit};
};

class LimitOrderBook {
public:
    explicit LimitOrderBook(size_t max_orders = 65536, size_t max_levels = 4096)
        : book_(lob_create(max_orders, max_levels)) {}

    ~LimitOrderBook() {
        if (book_) {
            lob_free(book_);
            book_ = nullptr;
        }
    }

    LimitOrderBook(const LimitOrderBook&) = delete;
    LimitOrderBook& operator=(const LimitOrderBook&) = delete;

    LimitOrderBook(LimitOrderBook&& other) noexcept : book_(other.book_) {
        other.book_ = nullptr;
    }

    LimitOrderBook& operator=(LimitOrderBook&& other) noexcept {
        if (this != &other) {
            if (book_) {
                lob_free(book_);
            }
            book_ = other.book_;
            other.book_ = nullptr;
        }
        return *this;
    }

    void clear() {
        if (book_) {
            lob_clear(book_);
        }
    }

    Result add_order(uint64_t id,
                     Side side,
                     uint64_t price,
                     uint32_t qty,
                     OrderType type = OrderType::Limit,
                     std::vector<Match>* matches = nullptr) {
        if (!book_) return Result::InvalidArg;
        auto cb = matches ? &match_recorder : nullptr;
        int res = lob_add_order(book_, id, static_cast<lob_side_t>(side), price, qty,
                                static_cast<lob_type_t>(type), cb, matches);
        return static_cast<Result>(res);
    }

    Result cancel_order(uint64_t id) {
        if (!book_) return Result::InvalidArg;
        return static_cast<Result>(lob_cancel_order(book_, id));
    }

    Result modify_order(uint64_t id,
                        uint32_t new_qty,
                        uint64_t new_price = 0,
                        std::vector<Match>* matches = nullptr) {
        if (!book_) return Result::InvalidArg;
        auto cb = matches ? &match_recorder : nullptr;
        return static_cast<Result>(lob_modify_order(book_, id, new_qty, new_price, cb, matches));
    }

    std::optional<OrderInfo> get_order(uint64_t id) const {
        if (!book_) return std::nullopt;
        const lob_order_t* ord = lob_find_order(book_, id);
        if (!ord) return std::nullopt;
        OrderInfo info;
        info.order_id = ord->order_id;
        info.price = ord->price;
        info.timestamp = ord->timestamp;
        info.qty = ord->qty;
        info.filled_qty = ord->filled_qty;
        info.side = static_cast<Side>(ord->side);
        info.status = static_cast<OrderStatus>(ord->status);
        info.type = static_cast<OrderType>(ord->type);
        return info;
    }

    std::optional<std::pair<uint64_t, uint32_t>> best_bid() const {
        if (!book_) return std::nullopt;
        uint64_t bp = 0;
        uint32_t bq = 0;
        uint64_t ap = 0;
        uint32_t aq = 0;
        if (lob_get_bbo(book_, &bp, &bq, &ap, &aq) && bp > 0) {
            return std::make_pair(bp, bq);
        }
        return std::nullopt;
    }

    std::optional<std::pair<uint64_t, uint32_t>> best_ask() const {
        if (!book_) return std::nullopt;
        uint64_t bp = 0;
        uint32_t bq = 0;
        uint64_t ap = 0;
        uint32_t aq = 0;
        if (lob_get_bbo(book_, &bp, &bq, &ap, &aq) && ap > 0) {
            return std::make_pair(ap, aq);
        }
        return std::nullopt;
    }

    std::optional<uint64_t> spread() const {
        if (!book_) return std::nullopt;
        uint64_t sp = lob_get_spread(book_);
        if (sp == UINT64_MAX) return std::nullopt;
        return sp;
    }

    std::optional<uint64_t> mid_price() const {
        if (!book_) return std::nullopt;
        uint64_t mp = lob_get_mid_price(book_);
        if (mp == 0) return std::nullopt;
        return mp;
    }

    std::vector<Level> get_depth(Side side, size_t max_levels = 10) const {
        std::vector<Level> result;
        if (!book_ || max_levels == 0) return result;
        std::vector<uint64_t> prices(max_levels);
        std::vector<uint64_t> quantities(max_levels);
        std::vector<uint32_t> counts(max_levels);
        size_t n = lob_get_depth(book_, static_cast<lob_side_t>(side), max_levels,
                                 prices.data(), quantities.data(), counts.data());
        result.reserve(n);
        for (size_t i = 0; i < n; i++) {
            result.push_back(Level{prices[i], quantities[i], counts[i]});
        }
        return result;
    }

    size_t order_count() const noexcept {
        return book_ ? lob_order_count(book_) : 0;
    }

    size_t level_count(Side side) const noexcept {
        return book_ ? lob_level_count(book_, static_cast<lob_side_t>(side)) : 0;
    }

    uint64_t total_volume() const noexcept {
        return book_ ? lob_total_volume(book_) : 0;
    }

    uint64_t trade_count() const noexcept {
        return book_ ? lob_trade_count(book_) : 0;
    }

    lob_book_t* handle() noexcept { return book_; }
    const lob_book_t* handle() const noexcept { return book_; }

private:
    static void match_recorder(const lob_match_t* m, void* user_data) {
        if (m && user_data) {
            auto* vec = static_cast<std::vector<Match>*>(user_data);
            vec->push_back(Match{
                m->maker_order_id,
                m->taker_order_id,
                m->price,
                m->qty,
                static_cast<Side>(m->taker_side)
            });
        }
    }

    lob_book_t* book_{nullptr};
};

class LockFreeOrderQueue {
public:
    explicit LockFreeOrderQueue(size_t capacity = 1024)
        : queue_(lob_queue_create(capacity)) {}

    ~LockFreeOrderQueue() {
        if (queue_) {
            lob_queue_free(queue_);
            queue_ = nullptr;
        }
    }

    LockFreeOrderQueue(const LockFreeOrderQueue&) = delete;
    LockFreeOrderQueue& operator=(const LockFreeOrderQueue&) = delete;

    LockFreeOrderQueue(LockFreeOrderQueue&& other) noexcept : queue_(other.queue_) {
        other.queue_ = nullptr;
    }

    LockFreeOrderQueue& operator=(LockFreeOrderQueue&& other) noexcept {
        if (this != &other) {
            if (queue_) {
                lob_queue_free(queue_);
            }
            queue_ = other.queue_;
            other.queue_ = nullptr;
        }
        return *this;
    }

    bool push(uint64_t id, Side side, uint64_t price, uint32_t qty, OrderType type = OrderType::Limit) {
        if (!queue_) return false;
        lob_command_t cmd;
        cmd.type = LOB_CMD_ADD;
        cmd.side = static_cast<uint8_t>(side);
        cmd.order_type = static_cast<uint8_t>(type);
        cmd._pad = 0;
        cmd.qty = qty;
        cmd.order_id = id;
        cmd.price = price;
        return lob_queue_push(queue_, &cmd);
    }

    bool push_cancel(uint64_t id) {
        if (!queue_) return false;
        lob_command_t cmd;
        cmd.type = LOB_CMD_CANCEL;
        cmd.side = 0;
        cmd.order_type = 0;
        cmd._pad = 0;
        cmd.qty = 0;
        cmd.order_id = id;
        cmd.price = 0;
        return lob_queue_push(queue_, &cmd);
    }

    bool pop(lob_command_t& cmd) {
        return queue_ ? lob_queue_pop(queue_, &cmd) : false;
    }

    size_t process(LimitOrderBook& book, size_t max_cmds = 0, std::vector<Match>* matches = nullptr) {
        if (!queue_ || !book.handle()) return 0;
        auto cb = matches ? &match_recorder : nullptr;
        return lob_process_queue(book.handle(), queue_, max_cmds, cb, matches);
    }

private:
    static void match_recorder(const lob_match_t* m, void* user_data) {
        if (m && user_data) {
            auto* vec = static_cast<std::vector<Match>*>(user_data);
            vec->push_back(Match{
                m->maker_order_id,
                m->taker_order_id,
                m->price,
                m->qty,
                static_cast<Side>(m->taker_side)
            });
        }
    }

    lob_queue_t* queue_{nullptr};
};

} // namespace qforge

#endif /* ORDER_BOOK_HPP */
