# 🥙 tacozip

**tacozip** is a high-performance, CIP64 (always ZIP64) archive writer with a fixed 64-byte "ghost" Local File Header (LFH). It is designed for fast, large-scale data packaging with predictable layout.

Key features:
- **Always ZIP64** — no 4 GB limits.
- **STORE-only** — zero-compression for maximum speed.
- **Fixed ghost LFH** — reserves 64 bytes for application metadata (offset, length, etc.).
- **C API + Python bindings** — use it directly from C, Python, or wrap in other languages.
- **Cross-platform** — Linux, macOS, Windows.
