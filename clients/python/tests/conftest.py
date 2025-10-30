import pytest
import sys
import os
from unittest.mock import Mock, patch
from pathlib import Path
import tempfile
import shutil


# Environment variable to enable heavy ZIP64 tests
ENABLE_ZIP64_TESTS = os.getenv("TACOZIP_ENABLE_ZIP64_TESTS", "false").lower() == "true"


# Print info about ZIP64 tests on collection
def pytest_collection_modifyitems(config, items):
    """Mark and display info about ZIP64 tests."""
    zip64_count = sum(1 for item in items if "zip64" in item.keywords)

    if zip64_count > 0 and not ENABLE_ZIP64_TESTS:
        print(
            f"\nSkipping {zip64_count} ZIP64 test(s). To enable: export TACOZIP_ENABLE_ZIP64_TESTS=true"
        )
    elif zip64_count > 0 and ENABLE_ZIP64_TESTS:
        print(
            f"\nRunning {zip64_count} ZIP64 test(s). This will create ~8GB files and take 10-15 minutes."
        )


@pytest.fixture
def mock_library():
    """Fixture providing a mock C library."""
    mock_lib = Mock()

    # Set up default return values for current API functions
    mock_lib.tacozip_get_version.return_value = b"0.9.0"
    mock_lib.tacozip_create.return_value = 0
    mock_lib.tacozip_update_header.return_value = 0
    mock_lib.tacozip_read_header.return_value = 0
    mock_lib.tacozip_parse_header.return_value = 0
    mock_lib.tacozip_detect_format.return_value = 1  # ZIP32 by default
    mock_lib.tacozip_validate.return_value = 0  # TACOZ_VALID

    # Add all required function attributes that match our current C API
    required_functions = [
        "tacozip_get_version",
        "tacozip_create",
        "tacozip_update_header",
        "tacozip_read_header",
        "tacozip_parse_header",
        "tacozip_detect_format",
        "tacozip_validate",
    ]

    for func_name in required_functions:
        if not hasattr(mock_lib, func_name):
            if func_name == "tacozip_get_version":
                setattr(mock_lib, func_name, Mock(return_value=b"1.0.0"))
            elif func_name == "tacozip_detect_format":
                setattr(mock_lib, func_name, Mock(return_value=1))
            else:
                setattr(mock_lib, func_name, Mock(return_value=0))

    return mock_lib


@pytest.fixture
def temp_dir():
    """Fixture providing a temporary directory."""
    temp_dir = tempfile.mkdtemp()
    yield Path(temp_dir)
    shutil.rmtree(temp_dir, ignore_errors=True)


@pytest.fixture
def sample_files(temp_dir):
    """Fixture providing sample files for testing."""
    files = []
    for i in range(3):
        file_path = temp_dir / f"sample_{i}.txt"
        file_path.write_text(f"Sample content {i}")
        files.append(str(file_path))
    return files


@pytest.fixture
def small_files(temp_dir):
    """Fixture providing small test files (< 1MB total)."""
    files = []
    for i in range(5):
        file_path = temp_dir / f"small_{i}.txt"
        content = f"Small test file {i}\n" * 100  # ~2KB each
        file_path.write_text(content)
        files.append(str(file_path))
    return files


@pytest.fixture
def large_files(temp_dir):
    """Fixture providing large files that would trigger ZIP64 (only if enabled).

    Creates 5 files of 1GB each = 5GB total (forces ZIP64 format).
    Skips if TACOZIP_ENABLE_ZIP64_TESTS is not set to 'true'.
    """
    if not ENABLE_ZIP64_TESTS:
        pytest.skip("ZIP64 tests disabled (set TACOZIP_ENABLE_ZIP64_TESTS=true)")

    files = []
    print("\nCreating large test files (5GB)...")

    # Create 5 files of 1GB each = 5GB total (forces ZIP64)
    for i in range(5):
        file_path = temp_dir / f"large_{i}.bin"
        print(f"  Creating {file_path.name} (1GB)...")

        # Write 1GB of data in chunks to avoid memory issues
        with open(file_path, "wb") as f:
            chunk = b"\x00" * (1024 * 1024)  # 1MB chunks
            for chunk_num in range(1024):  # 1024 chunks = 1GB
                f.write(chunk)
                # Print progress every 100MB
                if chunk_num % 100 == 0 and chunk_num > 0:
                    print(f"    {chunk_num}MB written...")

        files.append(str(file_path))
        print(f"  {file_path.name} created successfully")

    print("All large files created\n")
    return files


@pytest.fixture(autouse=True)
def mock_native_library():
    """Auto-use fixture to mock the native library loading."""
    with patch("tacozip.loader._load_shared") as mock_load:
        mock_lib = Mock()

        # Add all required functions that match our current C API
        required_functions = [
            "tacozip_get_version",
            "tacozip_create",
            "tacozip_update_header",
            "tacozip_read_header",
            "tacozip_parse_header",
            "tacozip_detect_format",
            "tacozip_validate",
        ]

        for func_name in required_functions:
            if func_name == "tacozip_get_version":
                func_mock = Mock(return_value=b"1.0.0")
            elif func_name == "tacozip_detect_format":
                func_mock = Mock(return_value=1)  # ZIP32
            else:
                func_mock = Mock(return_value=0)
            setattr(mock_lib, func_name, func_mock)

        mock_load.return_value = mock_lib
        yield mock_lib


# Custom pytest markers
def pytest_configure(config):
    """Register custom markers."""
    config.addinivalue_line(
        "markers", "zip64: tests that create large files for ZIP64 format"
    )
    config.addinivalue_line("markers", "slow: tests that take more than 30 seconds")
    config.addinivalue_line("markers", "manual: tests that require manual execution")


if __name__ == "__main__":
    import subprocess
    import sys

    # Command to run tests with coverage
    cmd = [
        sys.executable,
        "-m",
        "pytest",
        "tests/",
        "--cov=tacozip",
        "--cov-report=html:htmlcov",
        "--cov-report=term-missing",
        "--cov-report=xml:coverage.xml",
        "--cov-fail-under=100",
        "-v",
        "--tb=short",
    ]

    print("Running tacozip test suite with 100% coverage requirement...")
    print(f"Command: {' '.join(cmd)}")

    try:
        result = subprocess.run(cmd, check=True)
        print("\nAll tests passed with 100% coverage!")
        sys.exit(0)
    except subprocess.CalledProcessError as e:
        print(f"\nTests failed with exit code {e.returncode}")
        sys.exit(e.returncode)
