# tacozip

[![PyPI](https://img.shields.io/pypi/v/tacozip.svg)](https://pypi.python.org/pypi/tacozip)
[![PyPI - Wheel](https://img.shields.io/pypi/wheel/tacozip)](https://pypi.org/project/tacozip/#files)
[![Tests](https://github.com/tacofoundation/tacozip/actions/workflows/test_py.yml/badge.svg)](https://github.com/tacofoundation/tacozip/actions/workflows/test_py.yml)
[![codecov](https://codecov.io/gh/tacofoundation/tacozip/graph/badge.svg?token=cFqgSRDqmC)](https://codecov.io/gh/tacofoundation/tacozip)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Code style: black](https://img.shields.io/badge/code%20style-black-000000.svg)](https://github.com/psf/black)
[![BlueSky](https://img.shields.io/badge/bluesky-tacofoundation-1185fe?labelColor=000000&logo=bluesky)](https://bsky.app/profile/tacofoundation.bsky.social)


Regular ZIP (STORE-only) writer with libzip backend and TACO Header at byte 0 (see [TACO spec](https://tacofoundation.github.io/specification.html). Useful when the Central Directory scanning is too slow.

## 🚀 Quick Start

### Installation

```bash
pip install tacozip
```

## 🛠️ Development

### Requirements
- CMake 3.15+
- C compiler (GCC, Clang, MSVC)
- Python 3.8+

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

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](CONTRIBUTING.md) for details.

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details.


## 🔍 Benchmark

Performance comparison across different ZIP handling approaches:

[![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/drive/1MVt0uyi8Dmu_hIpNwqj1T4rw0ifFqBG-?usp=sharing)


---

*Built with ❤️ for the taco team*
