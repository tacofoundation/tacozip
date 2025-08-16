# Contributing to tacozip

Thank you for your interest in contributing to **tacozip**! We welcome bug reports, feature requests, pull requests, and improvements across both the C library and Python bindings.

## 🧾 How to Contribute

### 1. Fork and Clone

```bash
# Fork on GitHub, then:
git clone https://github.com/your-username/tacozip.git
cd tacozip
git checkout -b my-feature-branch
```

### 2. Development Setup

* Install CMake (3.15+)
* Make sure you have a C compiler (GCC, Clang, or MSVC)
* Create a virtual environment for Python

```bash
pip install -e clients/python/  # editable install
pip install -r clients/python/requirements-dev.txt  # for tests, linters
```

### 3. Build the C Library

```bash
cmake --preset release
cmake --build --preset release -j
```

### 4. Running Tests

#### Python tests can be run using:

```bash
# Python tests
cd clients/python
pytest

# C self-check
python -c "import tacozip; tacozip.self_check()"
```

## 🧪 Code Style

* Python code follows **Black** and **flake8** conventions.
* C code follows a simple 4-space indentation style (no tabs).
* Run `make format` if provided, or use `scripts/format.sh`.

## ✅ Pull Request Checklist

Before opening a PR:

* [ ] All tests pass locally
* [ ] Added/updated docs if needed
* [ ] Squash commits into clean history (rebase preferred)
* [ ] Title/description clearly explain the change

## 🐛 Reporting Issues

If you find a bug or want to request a feature, open an issue with:

* Clear description of the problem
* Steps to reproduce (if possible)
* Environment info (OS, compiler, Python version)

## 💬 Communication

We discuss design ideas and roadmap in GitHub Issues and Discussions. Feel free to join the conversation.

---

Thanks again for contributing to **tacozip** — your help makes the project better for everyone.
