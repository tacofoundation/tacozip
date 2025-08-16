# tacozip

**tacozip** is a specialized ZIP64 archive writer designed for efficient packaging of large datasets with embedded metadata. By storing metadata pointers directly in the archive header, it enables direct data access without requiring a separate Central File Directory (CFD) scan. Perfect for data pipelines that need fast, uncompressed storage with multiple index references.

## ✨ Why tacozip?

- 🚀 **Zero compression overhead** — STORE-only for maximum I/O throughput.
- 📊 **Multi-metadata support** — Store up to 7 external index pointers in a single archive.
- 📦 **No size limits** — Always ZIP64, handles files larger than 4GB.
- ⚡ **High performance** — Native C library with Python bindings (R and Julia coming soon!).
- 🌍 **Cross-platform** — Works on Linux, macOS, and Windows.

## 🚀 Quick Start

### Installation

```bash
pip install tacozip
```

### Basic Usage

```python
import tacozip

# Create archive with multiple metadata references
files = ["data1.parquet", "data2.parquet", "data3.parquet"]
names = ["part1.parquet", "part2.parquet", "part3.parquet"]

# Metadata pointers (offset, length) for external indices
offsets = [1000, 2500, 4200, 0, 0, 0, 0]  # Up to 7 entries
lengths = [300, 150, 800, 0, 0, 0, 0]     # 0 = unused slot

# Create archive
tacozip.create_multi(
    "dataset.taco.zip",
    files, names,
    offsets, lengths
)

# Read metadata later
metadata = tacozip.read_ghost_multi("dataset.taco.zip")
print(f"Found {metadata.count} metadata entries")
for i in range(metadata.count):
    print(f"Entry {i}: offset={metadata.entries[i].offset}, length={metadata.entries[i].length}")
```

## 📋 API Reference

### Multi-Entry API

| Function | Description |
|----------|-------------|
| `create_multi(zip_path, src_files, arc_files, offsets, lengths)` | Create archive with up to 7 metadata entries |
| `read_ghost_multi(zip_path)` | Read all metadata entries from archive |
| `update_ghost_multi(zip_path, offsets, lengths)` | Update metadata entries in existing archive |

### Single-Entry API

| Function | Description |
|----------|-------------|
| `create(zip_path, src_files, arc_files, offset, length)` | Create archive with single metadata entry |
| `read_ghost(zip_path)` | Read first metadata entry |
| `update_ghost(zip_path, offset, length)` | Update first metadata entry |

## 🏗️ Architecture

tacozip uses a special "TACO Ghost" entry at the beginning of each archive to store metadata pointers:

<img width="558" height="690" alt="TACOv3-Page-15 drawio" src="https://github.com/user-attachments/assets/b9d6d6e5-da6a-41aa-87d9-1f4bdb05ace7" />

The ghost entry contains:
- **Count byte** — Number of valid metadata entries (0-7)
- **7 offset/length pairs** — Pointers to external metadata files
- **116-byte payload** — Fixed size for consistent parsing

## 🛠️ Development

### Requirements
- CMake 3.15+
- C compiler (GCC, Clang, MSVC)
- Python 3.9+

### Build from Source

```bash
# Clone repository
git clone https://github.com/your-org/tacozip.git
cd tacozip

# Build C library
cmake --preset release
cmake --build --preset release -j

# Install Python package in development mode
pip install -e clients/python/
```

### Running Tests

```bash
# Python tests
cd clients/python
python -m pytest tests/

# C library self-check
python -c "import tacozip; tacozip.self_check()"
```

## 🎯 Use Cases

- **Data lakes** — Package multiple files with shared metadata
- **ML datasets** — Store training data with feature indices
- **Analytics pipelines** — Bundle data with multiple index files
- **Archive systems** — High-throughput storage without compression overhead

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](CONTRIBUTING.md) for details.

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details.

---

*Built with ❤️ for the taco team*