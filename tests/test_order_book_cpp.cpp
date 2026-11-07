/* ============================================================================
 * test_order_book_cpp.cpp — C++17 Unit Tests for LimitOrderBook Wrapper
 * ============================================================================ */

#include "order_book.hpp"
#include "pcap_parser.hpp"
#include "risk_engine.h"

#include <cassert>
#include <cstdio>
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

void test_cpp_simd_matching() {
    const char* backend = qforge::LimitOrderBook::simd_backend_name();
    assert(backend != nullptr);
    assert(std::string(backend).length() > 0);

    // Vectorized quantity matching helper
    std::vector<uint32_t> makers = {10, 20, 30, 40};
    std::vector<uint32_t> matched;
    std::vector<uint32_t> rem_makers;
    uint32_t rem_taker = 0;

    uint32_t total = qforge::LimitOrderBook::match_quantities_simd(
        25, makers, matched, rem_makers, &rem_taker);
    assert(total == 25);
    assert(rem_taker == 0);
    assert(matched.size() == 4);
    assert(matched[0] == 10 && matched[1] == 15 && matched[2] == 0 && matched[3] == 0);
    assert(rem_makers[0] == 0 && rem_makers[1] == 5 && rem_makers[2] == 30 && rem_makers[3] == 40);
}

void test_cpp_pcap_reader_writer() {
    const std::string tmp_file = "test_cpp_temp.pcap";
    {
        qforge::PcapWriter writer(tmp_file, true);
        assert(writer.is_valid());

        std::vector<uint8_t> pkt1 = {0x01, 0x02, 0x03, 0x04};
        std::vector<uint8_t> pkt2 = {0x05, 0x06, 0x07, 0x08, 0x09};

        assert(writer.write_packet(1000000000ULL, pkt1) == qforge::PcapStatus::Ok);
        assert(writer.write_packet(1000000050ULL, pkt2) == qforge::PcapStatus::Ok);
    }

    {
        qforge::PcapReader reader(tmp_file);
        assert(reader.is_valid());
        assert(reader.is_nanosecond());

        auto p1 = reader.next_packet();
        assert(p1.has_value());
        assert(p1->timestamp_ns == 1000000000ULL);
        assert(p1->incl_len == 4);

        auto p2 = reader.next_packet();
        assert(p2.has_value());
        assert(p2->timestamp_ns == 1000000050ULL);
        assert(p2->incl_len == 5);

        auto p3 = reader.next_packet();
        assert(!p3.has_value());
    }

    std::remove(tmp_file.c_str());
}

void test_cpp_pcap_replayer_to_book() {
    const std::string tmp_file = "test_cpp_feed.pcap";
    {
        qforge::PcapWriter writer(tmp_file, true);
        uint8_t dst_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        uint8_t src_mac[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};

        // Write ITCH Add Buy Order
        uint8_t pkt[128];
        size_t elen = pcap_encode_ethernet(pkt, sizeof(pkt), dst_mac, src_mac, PCAP_ETHERTYPE_IPV4);
        size_t ilen = pcap_encode_ipv4(pkt + elen, sizeof(pkt) - elen, 0x0A000001, 0x0A000002, PCAP_PROTO_UDP, 8 + 36);
        size_t ulen = pcap_encode_udp(pkt + elen + ilen, sizeof(pkt) - elen - ilen, 5000, 5001, 36);
        pcap_encode_itch_add(pkt + elen + ilen + ulen, sizeof(pkt) - elen - ilen - ulen,
                             1000, 1001, LOB_SIDE_BUY, 100, "AAPL", 15000);
        writer.write_packet(1000, pkt, static_cast<uint32_t>(elen + ilen + ulen + 36));

        // Write ITCH Add Sell Order
        pcap_encode_itch_add(pkt + elen + ilen + ulen, sizeof(pkt) - elen - ilen - ulen,
                             2000, 1002, LOB_SIDE_SELL, 200, "AAPL", 15500);
        writer.write_packet(2000, pkt, static_cast<uint32_t>(elen + ilen + ulen + 36));
    }

    {
        qforge::PcapReader reader(tmp_file);
        assert(reader.is_valid());

        qforge::LimitOrderBook book(64, 32);
        qforge::FeedReplayer replayer(reader, 0.0);
        assert(replayer.is_valid());

        auto status = replayer.run_into_order_book(book);
        assert(status == qforge::PcapStatus::Ok);

        assert(book.order_count() == 2);
        assert(book.best_bid().has_value() && book.best_bid()->first == 15000);
        assert(book.best_ask().has_value() && book.best_ask()->first == 15500);

        auto stats = replayer.stats();
        assert(stats.total_packets == 2);
        assert(stats.feed_messages == 2);
    }

    std::remove(tmp_file.c_str());
}

void test_cpp_risk_lifecycle() {
    const risk_limits_t limits = {100, 10000, 100, 5000, 2500};
    risk_engine_t *engine = risk_create(&limits, 8, 1000, 100);
    assert(engine != nullptr);
    assert(risk_reserve(engine, 1, LOB_SIDE_BUY, 100, 20) == RISK_OK);
    assert(risk_fill(engine, 1, 100, 10) == RISK_OK);
    assert(risk_release(engine, 1) == RISK_OK);
    assert(risk_mark(engine, 80) == RISK_OK);
    risk_state_t state{};
    assert(risk_get_state(engine, &state) == RISK_OK);
    assert(state.position == 10 && state.cash == 0 && state.equity == 800);
    assert(state.initial_margin == 400 && state.maintenance_margin == 200);
    assert(!state.maintenance_breached && state.open_orders == 0);
    risk_free(engine);
}

int main() {
    std::cout << "[C++ Tests] Running LimitOrderBook C++17 test suite..." << std::endl;
    test_cpp_basic_lifecycle();
    std::cout << "  [PASS] test_cpp_basic_lifecycle" << std::endl;
    test_cpp_matching_and_events();
    std::cout << "  [PASS] test_cpp_matching_and_events" << std::endl;
    test_cpp_queue_and_move_semantics();
    std::cout << "  [PASS] test_cpp_queue_and_move_semantics" << std::endl;
    test_cpp_simd_matching();
    std::cout << "  [PASS] test_cpp_simd_matching (" << qforge::LimitOrderBook::simd_backend_name() << ")" << std::endl;
    test_cpp_pcap_reader_writer();
    std::cout << "  [PASS] test_cpp_pcap_reader_writer" << std::endl;
    test_cpp_pcap_replayer_to_book();
    std::cout << "  [PASS] test_cpp_pcap_replayer_to_book" << std::endl;
    test_cpp_risk_lifecycle();
    std::cout << "  [PASS] test_cpp_risk_lifecycle" << std::endl;
    std::cout << "[C++ Tests] All C++17 tests passed successfully!" << std::endl;
    return 0;
}
