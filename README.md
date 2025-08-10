# tacozip

**tacozip** is a high-performance ZIP64 archive writer with specialized metadata support for large-scale data packaging. It features a fixed "TACO Ghost" Local File Header that can store up to 7 metadata entries for external indices.

## Key Features

- **Always ZIP64** — no 4GB size limits
- **STORE-only** — zero compression for maximum throughput  
- **Multi-metadata support** — up to 7 parquet metadata entries in ghost header
- **Cross-platform** — Linux, macOS, Windows
- **C library + Python bindings** — fast native performanceo

## API Reference

### Multi-Parquet API (Recommended)

- `tacozip_create_multi(zip_path, src_files, arc_files, meta_offsets, meta_lengths)` — Create archive with up to 7 metadata entries
- `tacozip_read_ghost_multi(zip_path)` — Read all metadata entries  
- `tacozip_update_ghost_multi(zip_path, meta_offsets, meta_lengths)` — Update metadata entries

### Legacy API (Single Entry)

- `create(zip_path, src_files, arc_files, meta_offset, meta_length)` — Create archive with single metadata entry
- `read_ghost(zip_path)` — Read first metadata entry
- `update_ghost(zip_path, new_offset, new_length)` — Update first metadata entry

## Build from Source

Requirements:
- CMake 3.15+
- C compiler (GCC, Clang, MSVC)
- Python 3.8+

```bash
git clone https://github.com/your-org/tacozip.git
cd tacozip

# Build C library
cmake --preset release
cmake --build --preset release -j

# Install Python package
pip install -e clients/python/
```

## License

MIT License