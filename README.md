# 🥙 tacozip

**tacozip** is a high-performance, ZIP64 archive writer with a fixed 64-byte "ghost" Local File Header (LFH). It is designed for fast, large-scale data packaging with predictable layout.

Key features:
- **Always ZIP64** — no 4 GB limits.
- **STORE-only** — zero-compression.
- **Fixed ghost LFH** — reserves 64 bytes for application metadata (offset, length, etc.).
- **C API + bindings** — use it directly from C, Python, or wrap in other languages.
- **Cross-platform** — Linux, macOS, Windows.

## Installation

### Quick Install (Recommended)

```bash
pip install tacozip
```

This will automatically download a prebuilt wheel for your platform if available.

### Build from Source

If no prebuilt wheel is available for your platform, pip will automatically build from source. This requires:

#### Prerequisites

- **CMake 3.15+**: `pip install cmake` or install via your system package manager
- **C compiler**: GCC, Clang, or MSVC
- **Build tools**:
  - Linux: `build-essential` or similar
  - macOS: Xcode command line tools (`xcode-select --install`)
  - Windows: Visual Studio or Build Tools for Visual Studio

#### Manual Build

If you want to build manually:

```bash
# Clone the repository
git clone https://github.com/your-org/tacozip.git
cd tacozip

# Build the C library
cmake --preset release
cmake --build --preset release -j

# Install the Python package in development mode
pip install -e clients/python/
```

#### Docker

For containerized environments, you can use the multi-stage build:

```dockerfile
FROM python:3.11-slim as builder
RUN apt-get update && apt-get install -y cmake build-essential
COPY . /src
WORKDIR /src
RUN pip install clients/python/

FROM python:3.11-slim
COPY --from=builder /usr/local/lib/python3.11/site-packages/tacozip /usr/local/lib/python3.11/site-packages/tacozip
RUN pip install tacozip --no-deps
```

#### Troubleshooting

**CMake not found**: Install CMake with `pip install cmake` or your system package manager.

**Compiler errors**: Ensure you have a compatible C compiler installed:
- Ubuntu/Debian: `sudo apt install build-essential`
- CentOS/RHEL: `sudo yum groupinstall "Development Tools"`
- macOS: `xcode-select --install`
- Windows: Install Visual Studio Build Tools

**Permission errors**: Try `pip install --user tacozip` to install to user directory.

**Platform not supported**: Open an issue on GitHub with your platform details.