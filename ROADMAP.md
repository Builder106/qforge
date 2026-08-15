# QForge Roadmap

High-performance quantitative engine and market simulator roadmap.

## v1.1 — Order Book Engine Optimizations
- **L3 Limit Order Book**: Lock-free cache-aligned order book implementation in C++/Rust.
- **SIMD Matching Engine**: Vectorized instruction optimization for sub-microsecond matching latency.

## v1.2 — Market Feed Handlers
- **PCAP Binary Parser**: Nanosecond packet capture replay for exchange feed testing.
- **Risk Limit Engine**: Real-time margin and pre-trade risk controls.

## Out of Scope
- Cloud-hosted web dashboards (native/CLI first)
- Non-deterministic floating-point math in matching path

---
For technical specifications, see [`docs/specs/`](docs/specs/).
