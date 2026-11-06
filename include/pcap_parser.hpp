/* ============================================================================
 * pcap_parser.hpp — C++17 RAII Wrapper for PCAP Binary Parser & Feed Replayer
 *
 * Provides a modern, type-safe C++17 interface for PCAP capture parsing,
 * network frame decoding, and exchange feed simulation.
 * ============================================================================ */

#ifndef PCAP_PARSER_HPP
#define PCAP_PARSER_HPP

#include "order_book.hpp"
#include "pcap_parser.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace qforge {

enum class PcapStatus : int {
    Ok = PCAP_OK,
    InvalidArg = PCAP_ERR_INVALID_ARG,
    FileIo = PCAP_ERR_FILE_IO,
    BadMagic = PCAP_ERR_BAD_MAGIC,
    UnsupportedVersion = PCAP_ERR_UNSUPPORTED_VERSION,
    CorruptHeader = PCAP_ERR_CORRUPT_HEADER,
    TruncatedPacket = PCAP_ERR_TRUNCATED_PACKET,
    Eof = PCAP_ERR_EOF,
    BufferOverflow = PCAP_ERR_BUFFER_OVERFLOW,
    UnsupportedLinkType = PCAP_ERR_UNSUPPORTED_LINKTYPE,
    ParseFailed = PCAP_ERR_PARSE_FAILED
};

enum class FeedMsgType : uint8_t {
    Add = PCAP_FEED_MSG_ADD,
    Execute = PCAP_FEED_MSG_EXECUTE,
    Cancel = PCAP_FEED_MSG_CANCEL,
    Delete = PCAP_FEED_MSG_DELETE,
    Replace = PCAP_FEED_MSG_REPLACE,
    Trade = PCAP_FEED_MSG_TRADE
};

struct FeedMsg {
    FeedMsgType type{FeedMsgType::Add};
    uint64_t timestamp_ns{0};
    uint64_t order_id{0};
    uint64_t new_order_id{0};
    uint64_t price{0};
    uint32_t qty{0};
    uint32_t executed_qty{0};
    uint64_t match_id{0};
    Side side{Side::Buy};
    std::string symbol{};
};

/*
 * RAII Wrapper for PCAP File and Memory Buffer Reading
 */
class PcapReader {
public:
    explicit PcapReader(const std::string &filename)
        : reader_(pcap_reader_open(filename.c_str())) {}

    PcapReader(const void *buf, size_t size)
        : reader_(pcap_reader_open_memory(buf, size)) {}

    ~PcapReader() {
        if (reader_) {
            pcap_reader_close(reader_);
            reader_ = nullptr;
        }
    }

    PcapReader(const PcapReader &) = delete;
    PcapReader &operator=(const PcapReader &) = delete;

    PcapReader(PcapReader &&other) noexcept : reader_(other.reader_) {
        other.reader_ = nullptr;
    }

    PcapReader &operator=(PcapReader &&other) noexcept {
        if (this != &other) {
            if (reader_) {
                pcap_reader_close(reader_);
            }
            reader_ = other.reader_;
            other.reader_ = nullptr;
        }
        return *this;
    }

    bool is_valid() const noexcept { return reader_ != nullptr; }

    bool is_nanosecond() const noexcept {
        return reader_ ? pcap_reader_is_nanosecond(reader_) : false;
    }

    bool is_swapped() const noexcept {
        return reader_ ? pcap_reader_is_swapped(reader_) : false;
    }

    PcapStatus rewind() noexcept {
        if (!reader_) return PcapStatus::InvalidArg;
        return static_cast<PcapStatus>(pcap_reader_rewind(reader_));
    }

    std::optional<pcap_packet_t> next_packet() noexcept {
        if (!reader_) return std::nullopt;
        pcap_packet_t pkt;
        if (pcap_reader_next_packet(reader_, &pkt) == PCAP_OK) {
            return pkt;
        }
        return std::nullopt;
    }

    pcap_reader_t *raw_handle() noexcept { return reader_; }

private:
    pcap_reader_t *reader_{nullptr};
};

/*
 * RAII Wrapper for PCAP File Capture Writing
 */
class PcapWriter {
public:
    explicit PcapWriter(const std::string &filename, bool nanosecond = true,
                        uint32_t linktype = PCAP_LINKTYPE_ETHERNET)
        : writer_(pcap_writer_create(filename.c_str(), nanosecond, linktype)) {}

    ~PcapWriter() {
        if (writer_) {
            pcap_writer_close(writer_);
            writer_ = nullptr;
        }
    }

    PcapWriter(const PcapWriter &) = delete;
    PcapWriter &operator=(const PcapWriter &) = delete;

    PcapWriter(PcapWriter &&other) noexcept : writer_(other.writer_) {
        other.writer_ = nullptr;
    }

    PcapWriter &operator=(PcapWriter &&other) noexcept {
        if (this != &other) {
            if (writer_) {
                pcap_writer_close(writer_);
            }
            writer_ = other.writer_;
            other.writer_ = nullptr;
        }
        return *this;
    }

    bool is_valid() const noexcept { return writer_ != nullptr; }

    PcapStatus write_packet(uint64_t timestamp_ns, const void *data, uint32_t len) noexcept {
        if (!writer_) return PcapStatus::InvalidArg;
        return static_cast<PcapStatus>(
            pcap_writer_write_packet(writer_, timestamp_ns, data, len));
    }

    PcapStatus write_packet(uint64_t timestamp_ns, const std::vector<uint8_t> &data) noexcept {
        return write_packet(timestamp_ns, data.data(), static_cast<uint32_t>(data.size()));
    }

    pcap_writer_t *raw_handle() noexcept { return writer_; }

private:
    pcap_writer_t *writer_{nullptr};
};

/*
 * High-Precision Nanosecond Replay Engine Wrapper
 */
class FeedReplayer {
public:
    explicit FeedReplayer(PcapReader &reader, double speed_multiplier = 0.0)
        : reader_(reader) {
        config_ = {};
        config_.speed_multiplier = speed_multiplier;
        replayer_ = pcap_replayer_create(reader_.raw_handle(), &config_);
    }

    ~FeedReplayer() {
        if (replayer_) {
            pcap_replayer_free(replayer_);
            replayer_ = nullptr;
        }
    }

    FeedReplayer(const FeedReplayer &) = delete;
    FeedReplayer &operator=(const FeedReplayer &) = delete;

    FeedReplayer(FeedReplayer &&other) noexcept
        : reader_(other.reader_), replayer_(other.replayer_), config_(other.config_) {
        other.replayer_ = nullptr;
    }

    FeedReplayer &operator=(FeedReplayer &&other) noexcept {
        if (this != &other) {
            if (replayer_) {
                pcap_replayer_free(replayer_);
            }
            replayer_ = other.replayer_;
            config_ = other.config_;
            other.replayer_ = nullptr;
        }
        return *this;
    }

    bool is_valid() const noexcept { return replayer_ != nullptr; }

    void set_speed_multiplier(double mult) noexcept {
        config_.speed_multiplier = mult;
        if (replayer_) {
            pcap_replayer_free(replayer_);
            replayer_ = pcap_replayer_create(reader_.raw_handle(), &config_);
        }
    }

    void set_packet_callback(std::function<void(const pcap_packet_t &)> cb) {
        packet_cb_ = std::move(cb);
        config_.on_packet = [](const pcap_packet_t *pkt, void *user_data) {
            auto *self = static_cast<FeedReplayer *>(user_data);
            if (self && self->packet_cb_) {
                self->packet_cb_(*pkt);
            }
        };
        config_.user_data = this;
        pcap_replayer_free(replayer_);
        replayer_ = pcap_replayer_create(reader_.raw_handle(), &config_);
    }

    void set_feed_callback(std::function<void(const FeedMsg &)> cb) {
        feed_cb_ = std::move(cb);
        config_.on_feed_msg = [](const pcap_feed_msg_t *msg, void *user_data) {
            auto *self = static_cast<FeedReplayer *>(user_data);
            if (self && self->feed_cb_) {
                FeedMsg m;
                m.type = static_cast<FeedMsgType>(msg->type);
                m.timestamp_ns = msg->timestamp_ns;
                m.order_id = msg->order_id;
                m.new_order_id = msg->new_order_id;
                m.price = msg->price;
                m.qty = msg->qty;
                m.executed_qty = msg->executed_qty;
                m.match_id = msg->match_id;
                m.side = static_cast<Side>(msg->side);
                m.symbol = msg->symbol;
                self->feed_cb_(m);
            }
        };
        config_.user_data = this;
        pcap_replayer_free(replayer_);
        replayer_ = pcap_replayer_create(reader_.raw_handle(), &config_);
    }

    PcapStatus step() noexcept {
        if (!replayer_) return PcapStatus::InvalidArg;
        return static_cast<PcapStatus>(pcap_replayer_step(replayer_, nullptr));
    }

    PcapStatus run() noexcept {
        if (!replayer_) return PcapStatus::InvalidArg;
        return static_cast<PcapStatus>(pcap_replayer_run(replayer_));
    }

    /*
     * Replay feed messages directly into an L3 Limit Order Book
     */
    PcapStatus run_into_order_book(LimitOrderBook &book) {
        set_feed_callback([&book](const FeedMsg &msg) {
            pcap_feed_msg_t raw_msg;
            raw_msg.type = static_cast<pcap_feed_msg_type_t>(msg.type);
            raw_msg.timestamp_ns = msg.timestamp_ns;
            raw_msg.order_id = msg.order_id;
            raw_msg.new_order_id = msg.new_order_id;
            raw_msg.price = msg.price;
            raw_msg.qty = msg.qty;
            raw_msg.executed_qty = msg.executed_qty;
            raw_msg.match_id = msg.match_id;
            raw_msg.side = static_cast<uint8_t>(msg.side);
            pcap_feed_apply_to_book(book.handle(), &raw_msg);
        });
        return run();
    }

    pcap_replay_stats_t stats() const noexcept {
        pcap_replay_stats_t s{};
        if (replayer_) {
            pcap_replayer_get_stats(replayer_, &s);
        }
        return s;
    }

private:
    PcapReader &reader_;
    pcap_replayer_t *replayer_{nullptr};
    pcap_replay_config_t config_{};
    std::function<void(const pcap_packet_t &)> packet_cb_{};
    std::function<void(const FeedMsg &)> feed_cb_{};
};

} // namespace qforge

#endif /* PCAP_PARSER_HPP */
