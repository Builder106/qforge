# PCAP Binary Parser & Nanosecond Feed Replayer Specification

## 1. Overview

The QForge PCAP Binary Parser and Exchange Feed Replayer provides a high-throughput, zero-dependency network packet capture replay engine with nanosecond timestamp precision. It allows quantitative trading algorithms, matching engines, and risk controllers to be backtested and benchmarked against realistic historical exchange packet captures at line rate or with cycle-accurate pacing.

Interfaces:
- **C99 Core Interface**: `include/pcap_parser.h` and `src/pcap_parser.c`
- **C++17 RAII Wrapper**: `include/pcap_parser.hpp`
- **Benchmark & Simulator Tool**: `examples/pcap_replay_benchmark.c`

---

## 2. PCAP Binary Format & Architectural Invariants

### 2.1 File Header (24-byte Global Header)
Supports standard libpcap format captures with microsecond and nanosecond timestamps:

| Field | Size | Description |
| :--- | :--- | :--- |
| `magic_number` | 4 bytes | `0xa1b2c3d4` (microsecond native), `0xd4c3b2a1` (microsecond swapped)<br>`0xa1b23c4d` (nanosecond native), `0x4d3cb2a1` (nanosecond swapped) |
| `version_major` | 2 bytes | File format major version (2) |
| `version_minor` | 2 bytes | File format minor version (4) |
| `thiszone` | 4 bytes | GMT to local timezone offset correction |
| `sigfigs` | 4 bytes | Accuracy of timestamps in capture |
| `snaplen` | 4 bytes | Maximum length of captured packets (up to 65,535 bytes) |
| `network` | 4 bytes | Link-layer type (`LINKTYPE_ETHERNET = 1`, `LINKTYPE_RAW = 101`, `LINKTYPE_NULL = 0`, `LINKTYPE_LINUX_SLL = 113`) |

### 2.2 Packet Record (16-byte Packet Header)
Each packet in the stream consists of a 16-byte header followed by `incl_len` octets:

```text
+-------------------+-------------------+-------------------+-------------------+
|     ts_sec (4B)   |    ts_usec (4B)   |   incl_len (4B)   |   orig_len (4B)   |
+-------------------+-------------------+-------------------+-------------------+
|                            Packet Data (incl_len bytes)                       |
+-------------------------------------------------------------------------------+
```

- **Timestamp Normalization**: Unified 64-bit nanosecond representation:
  - Nanosecond captures: $t_{\text{ns}} = (t_{\text{sec}} \times 10^9) + t_{\text{usec}}$
  - Microsecond captures: $t_{\text{ns}} = (t_{\text{sec}} \times 10^9) + (t_{\text{usec}} \times 10^3)$
- **Zero-Copy Memory-Mapped / Buffer Parsing**: The parser supports both streaming filesystem I/O (`pcap_reader_open`) and direct in-memory zero-copy parsing (`pcap_reader_open_memory`).

---

## 3. Network Protocol Stack Decoders

The parser provides zero-copy decoding through Layer 2, Layer 3, and Layer 4:

1. **Layer 2 (Ethernet II & IEEE 802.1Q VLAN)**:
   - Destination MAC (6 bytes), Source MAC (6 bytes), EtherType (2 bytes).
   - Unnests 802.1Q VLAN tags (EtherType `0x8100`), extracting 12-bit VLAN IDs and encapsulated EtherTypes without extra heap allocations.
2. **Layer 3 (IPv4)**:
   - Extracts IHL, version, TTL, protocol (`IPPROTO_UDP = 17`, `IPPROTO_TCP = 6`), 32-bit Source IP, and 32-bit Destination IP.
   - Host-endian IP formatting and fast string conversion (`pcap_ip_to_str`, `pcap_str_to_ip`).
3. **Layer 4 (UDP & TCP)**:
   - UDP: Source port, Destination port, UDP length, transport payload pointer.
   - TCP: Source port, Destination port, sequence numbers, acknowledgment numbers, flags (SYN, ACK, FIN, RST, PSH), header offset, payload pointer.

---

## 4. Market Data Feed Protocols & Direct LOB Ingestion

### 4.1 NASDAQ TotalView-ITCH 5.0
Decodes standard equity exchange market data messages:
- `'A'`: Add Order (36 bytes) — Stock Locate, Order Reference Number, Buy/Sell, Shares, Symbol, 4-decimal Fixed-Point Price.
- `'F'`: Add Order with MPID Attribution (40 bytes).
- `'E'`: Order Executed (31 bytes) — Order Reference Number, Executed Shares, Match Number.
- `'C'`: Order Executed with Price (36 bytes).
- `'X'`: Order Cancel (23 bytes) — Partial share reduction.
- `'D'`: Order Delete (19 bytes) — Full order cancellation.
- `'U'`: Order Replace (35 bytes) — Atomic cancel-replace with priority migration.
- `'P'`: Non-Cross Trade (44 bytes).

### 4.2 MoldUDP64 Multicast Protocol
Unpacks MoldUDP64 downstream session framing (20-byte session header + repeated 2-byte length-prefixed ITCH messages).

### 4.3 Direct Order Book Ingestion (`pcap_feed_apply_to_book`)
Decoded feed messages map directly to the Level 3 Limit Order Book (`lob_book_t`):
- `PCAP_FEED_MSG_ADD` $\to$ `lob_add_order()`
- `PCAP_FEED_MSG_CANCEL` / `PCAP_FEED_MSG_EXECUTE` $\to$ `lob_modify_order()` or `lob_cancel_order()`
- `PCAP_FEED_MSG_DELETE` $\to$ `lob_cancel_order()`
- `PCAP_FEED_MSG_REPLACE` $\to$ cancel old order, add new order at new level

---

## 5. Nanosecond Replay Engine Architecture

### 5.1 Replay Timing Modes
- **AFAP Mode (`speed_multiplier = 0.0`)**: As-Fast-As-Possible batch execution at wire line rate for maximum ingestion benchmarking.
- **Paced Replay (`speed_multiplier > 0.0`)**: Cycle-accurate playback with exact inter-packet arrival gaps:
  $$\Delta t_{\text{sim}} = t_i - t_0$$
  $$t_{\text{target\_wall}} = t_{\text{wall0}} + \frac{\Delta t_{\text{sim}}}{\text{speed\_multiplier}}$$
- **Hybrid Pacing**: For intervals $\Delta t > 100\,\mu\text{s}$, yields the CPU using `nanosleep()` with a $50\,\mu\text{s}$ wake headroom, followed by high-resolution spin-wait polling on `clock_gettime(CLOCK_MONOTONIC)` to eliminate OS scheduler jitter.

### 5.2 Packet Filtering Pipeline
Filtering operates in constant time per packet before decoding transport payloads:
- Source / Destination IPv4 and CIDR subnets (`filter_ip_mask`).
- Source / Destination UDP/TCP port filters.
- Transport protocol filters (`IPPROTO_UDP`, `IPPROTO_TCP`).
- Bounded time window filtering (`start_time_ns` to `end_time_ns`).

---

## 6. Performance Profile & Benchmark Results

Measured on 64-bit Linux (`aarch64`, GCC `-O2`):

| Operation | Throughput | Latency | Bandwidth |
| :--- | :--- | :--- | :--- |
| **Pure PCAP Binary Parsing & Frame Decoding** | **~11.49M packets/sec** | **~87.1 ns / packet** | **~7,167 Mbps (~7.1 Gbps)** |
| **End-to-End Feed Replay + L3 Order Book Ingestion** | **~3.35M to 3.87M orders/sec** | **~258 - 298 ns / order** | — |
| **Synthetic PCAP Capture Generation** | **~8.0M packets/sec** | **~125 ns / packet** | — |

---

## 7. Verification & Sanitizers

The PCAP Parser and Replayer are validated through:
- 95 automated C unit tests verifying header variations, byte-swapped endianness, truncated frames, MoldUDP64 batching, and order book interactions.
- AddressSanitizer (ASan) memory leak check (`make memcheck`): zero leaks.
- UndefinedBehaviorSanitizer (UBSan): clean execution.
- C++17 test runner (`make test_cpp`) testing RAII `PcapReader`, `PcapWriter`, and `FeedReplayer`.
- Black-box integration tests (`make integration`).
- Strict warning flags: `-std=c99 -Wall -Wextra -Werror=deprecated-declarations -Werror -pedantic`.
