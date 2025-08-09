# 🥙 tacozip

**tacozip** is a high-performance, CIP64 (always ZIP64) archive writer with a fixed 64-byte "ghost" Local File Header (LFH).  
It is designed for fast, large-scale data packaging with predictable layout.

Key features:
- **Always ZIP64** — no 4 GB limits.
- **STORE-only** — zero-compression for maximum speed.
- **Fixed ghost LFH** — reserves 64 bytes for application metadata (offset, length, etc.).
- **C API + Python bindings** — use it directly from C, Python, or wrap in other languages.
- **Cross-platform** — Linux, macOS, Windows.

---

## 📦 Installation

From **PyPI**:

```bash
pip install tacozip
```

From TestPyPI (testing builds):

```bash
pip install --index-url https://test.pypi.org/simple --no-deps tacozip
```

From source:

```bash
git clone https://github.com/YOUR_USER/tacozip.git
cd tacozip
cmake --preset release
cmake --build --preset release -j
pip install ./python
```

## 🚀 Quickstart (Python)


```python
import tacozip
from pathlib import Path

# Input folder with files to archive
src_dir = Path("/path/to/data")
zip_out = Path("my_archive.taco.zip")

# File paths (source) and names (inside archive)
src_files = [str(f) for f in src_dir.iterdir() if f.is_file()]
arc_files = [f.name for f in src_dir.iterdir() if f.is_file()]

# Create the archive
rc = tacozip.create(
    str(zip_out),
    src_files,
    arc_files,
    meta_offset=0,    # placeholder offset in ghost LFH
    meta_length=0     # placeholder length in ghost LFH
)
print(f"Created: {zip_out} (rc={rc})")

# Read ghost metadata
rc, offset, length = tacozip.read_ghost(str(zip_out))
print(f"Ghost: offset={offset}, length={length}")

# Update ghost metadata in-place
tacozip.update_ghost(str(zip_out), new_offset=1234, new_length=5678)
print("Ghost metadata updated.")
```

## ⚙️ Quickstart (C)


```c
#include <tacozip.h>

int main() {
    const char *src_files[] = { "file1.bin", "file2.bin" };
    const char *arc_names[] = { "file1.bin", "file2.bin" };

    int rc = tacozip_create(
        "my_archive.taco.zip",
        src_files,
        arc_names,
        2,        // number of files
        0, 0      // meta_offset, meta_length
    );

    return rc;
}
```


Compile with:


```bash
gcc -Iinclude -Lbuild/release -ltacozip main.c -o example
```
