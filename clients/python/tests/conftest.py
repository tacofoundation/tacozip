# conftest.py - pytest configuration and fixtures
import pytest
import sys
from unittest.mock import Mock, patch
from pathlib import Path
import tempfile
import shutil


@pytest.fixture
def mock_library():
    """Fixture providing a mock C library."""
    mock_lib = Mock()
    
    # Set up default return values for actual functions
    mock_lib.tacozip_get_version.return_value = b"0.9.0"
    mock_lib.tacozip_create.return_value = 0
    mock_lib.tacozip_update_header.return_value = 0
    mock_lib.tacozip_read_header.return_value = 0
    mock_lib.tacozip_append_files.return_value = 0
    mock_lib.tacozip_trim_from.return_value = 0
    
    # Add all required function attributes that match our C API
    required_functions = [
        'tacozip_get_version',
        'tacozip_create', 
        'tacozip_update_header',
        'tacozip_read_header',
        'tacozip_append_files',
        'tacozip_trim_from'
    ]
    
    for func_name in required_functions:
        if not hasattr(mock_lib, func_name):
            if func_name == 'tacozip_get_version':
                setattr(mock_lib, func_name, Mock(return_value=b"1.0.0"))
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


@pytest.fixture(autouse=True)
def mock_native_library():
    """Auto-use fixture to mock the native library loading."""
    with patch('tacozip.loader._load_shared') as mock_load:
        mock_lib = Mock()
        
        # Add all required functions that match our actual C API
        required_functions = [
            'tacozip_get_version',
            'tacozip_create',
            'tacozip_update_header',
            'tacozip_read_header', 
            'tacozip_append_files',
            'tacozip_trim_from'
        ]
        
        for func_name in required_functions:
            if func_name == 'tacozip_get_version':
                func_mock = Mock(return_value=b"1.0.0")
            else:
                func_mock = Mock(return_value=0)
            setattr(mock_lib, func_name, func_mock)
        
        mock_load.return_value = mock_lib
        yield mock_lib


if __name__ == "__main__":
    import subprocess
    import sys
    
    # Command to run tests with coverage
    cmd = [
        sys.executable, "-m", "pytest",
        "tests/",
        "--cov=tacozip",
        "--cov-report=html:htmlcov",
        "--cov-report=term-missing",
        "--cov-report=xml:coverage.xml",
        "--cov-fail-under=100",
        "-v",
        "--tb=short"
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