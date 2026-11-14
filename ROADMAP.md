# QForge Roadmap

Plans for the quantitative engine and market simulator.

## v1.1 — Order Book Engine Optimizations

- [x] **L3 Limit Order Book**: Implement a lock-free, cache-aligned order book in C with a C++17 wrapper.
- [x] **SIMD Matching Engine**: Use vectorized instructions for sub-microsecond matching latency.

## v1.2 — Market Feed Handlers

- [x] **PCAP Binary Parser**: Replay packet captures at nanosecond precision for exchange feed testing.
- [x] **Risk Limit Engine**: Provide real-time margin and pre-trade risk controls.

## Out of Scope

- Cloud-hosted web dashboards (native/CLI first)
- Non-deterministic floating-point math in matching path

---
For technical specifications, see [`docs/specs/`](docs/specs/).
