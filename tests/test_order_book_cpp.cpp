/* ============================================================================
 * test_order_book_cpp.cpp — C++17 Unit Tests for LimitOrderBook Wrapper
 * ============================================================================ */

#include "order_book.hpp"

#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

void test_cpp_basic_lifecycle() {
    qforge::LimitOrderBook book(64, 32);
    assert(book.order_count() == 0);
    assert(!book.best_bid().has_value());
    assert(!book.best_ask().has_value());
    assert(!book.spread().has_value());
    assert(!book.mid_price().has_value());

    // Add Buy order
    auto res = book.add_order(101, qforge::Side::Buy, 100, 50);
    assert(res == qforge::Result::Ok);
    assert(book.order_count() == 1);
    assert(book.best_bid().has_value());
    assert(book.best_bid()->first == 100);
    assert(book.best_bid()->second == 50);

    // Add Sell order
    res = book.add_order(102, qforge::Side::Sell, 105, 20);
    assert(res == qforge::Result::Ok);
    assert(book.best_ask().has_value());
    assert(book.best_ask()->first == 105);
    assert(book.best_ask()->second == 20);
    assert(book.spread().value() == 5);
    assert(book.mid_price().value() == 102);

    // Order lookup
    auto ord = book.get_order(101);
    assert(ord.has_value());
    assert(ord->order_id == 101);
    assert(ord->price == 100);
    assert(ord->qty == 50);
    assert(ord->side == qforge::Side::Buy);
    assert(ord->status == qforge::OrderStatus::Active);

    // Cancel order
    assert(book.cancel_order(101) == qforge::Result::Ok);
    assert(!book.get_order(101).has_value());
    assert(book.order_count() == 1);

    // Clear
    book.clear();
    assert(book.order_count() == 0);
}

void test_cpp_matching_and_events() {
    qforge::LimitOrderBook book(64, 32);

    // Add 2 resting sell orders
    book.add_order(1, qforge::Side::Sell, 100, 10);
    book.add_order(2, qforge::Side::Sell, 100, 20);

    std::vector<qforge::Match> matches;
    auto res = book.add_order(10, qforge::Side::Buy, 100, 15, qforge::OrderType::Limit, &matches);
    assert(res == qforge::Result::Ok);
    assert(matches.size() == 2);
    assert(matches[0].maker_order_id == 1);
    assert(matches[0].qty == 10);
    assert(matches[1].maker_order_id == 2);
    assert(matches[1].qty == 5);

    // Depth query
    auto depth = book.get_depth(qforge::Side::Sell, 5);
    assert(depth.size() == 1);
    assert(depth[0].price == 100);
    assert(depth[0].total_qty == 15);
    assert(depth[0].order_count == 1);
}

void test_cpp_queue_and_move_semantics() {
    qforge::LockFreeOrderQueue queue(16);
    assert(queue.push(1, qforge::Side::Buy, 100, 10));
    assert(queue.push(2, qforge::Side::Buy, 101, 20));

    qforge::LimitOrderBook book(64, 32);
    size_t processed = queue.process(book);
    assert(processed == 2);
    assert(book.order_count() == 2);

    // Move semantics
    qforge::LimitOrderBook moved_book(std::move(book));
    assert(moved_book.order_count() == 2);
    assert(book.order_count() == 0);

    qforge::LimitOrderBook assigned_book;
    assigned_book = std::move(moved_book);
    assert(assigned_book.order_count() == 2);
}

int main() {
    std::cout << "[C++ Tests] Running LimitOrderBook C++17 test suite..." << std::endl;
    test_cpp_basic_lifecycle();
    std::cout << "  [PASS] test_cpp_basic_lifecycle" << std::endl;
    test_cpp_matching_and_events();
    std::cout << "  [PASS] test_cpp_matching_and_events" << std::endl;
    test_cpp_queue_and_move_semantics();
    std::cout << "  [PASS] test_cpp_queue_and_move_semantics" << std::endl;
    std::cout << "[C++ Tests] All C++17 tests passed successfully!" << std::endl;
    return 0;
}
