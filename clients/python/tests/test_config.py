"""Test configuration constants and error messages."""
import pytest
from tacozip import config


class TestConfig:
    """Test configuration constants."""
    
    def test_error_codes(self):
        """Test error code constants."""
        assert config.TACOZ_OK == 0
        assert config.TACOZ_ERR_IO == -1
        assert config.TACOZ_ERR_LIBZIP == -2
        assert config.TACOZ_ERR_INVALID_GHOST == -3
        assert config.TACOZ_ERR_PARAM == -4
        assert config.TACOZ_ERR_NOT_FOUND == -5
    
    def test_ghost_constants(self):
        """Test TACO Ghost constants."""
        assert config.TACO_GHOST_MAX_ENTRIES == 7
        assert config.TACO_GHOST_SIZE == 160
        assert config.TACO_GHOST_NAME == "TACO_GHOST"
        assert config.TACO_GHOST_NAME_LEN == 10
        assert config.TACO_GHOST_EXTRA_ID == 0x7454
        assert config.TACO_GHOST_EXTRA_SIZE == 116
    
    def test_error_messages(self):
        """Test error messages exist for all error codes."""
        assert config.TACOZ_ERR_IO in config.ERROR_MESSAGES
        assert config.TACOZ_ERR_INVALID_GHOST in config.ERROR_MESSAGES
        assert config.TACOZ_ERR_PARAM in config.ERROR_MESSAGES
        assert config.TACOZ_ERR_NOT_FOUND in config.ERROR_MESSAGES
        
        # Check messages are not empty
        for code, message in config.ERROR_MESSAGES.items():
            assert isinstance(message, str)
            assert len(message) > 0
    
    def test_library_names(self):
        """Test library names for different platforms."""
        assert "linux" in config.LIBRARY_NAMES
        assert "darwin" in config.LIBRARY_NAMES
        assert "win32" in config.LIBRARY_NAMES
        
        # Check Linux libraries
        linux_libs = config.LIBRARY_NAMES["linux"]
        assert isinstance(linux_libs, list)
        assert len(linux_libs) > 0
        assert all(lib.endswith(".so") for lib in linux_libs)
        
        # Check macOS libraries
        darwin_libs = config.LIBRARY_NAMES["darwin"]
        assert isinstance(darwin_libs, list)
        assert len(darwin_libs) > 0
        assert all(lib.endswith(".dylib") for lib in darwin_libs)
        
        # Check Windows libraries
        win32_libs = config.LIBRARY_NAMES["win32"]
        assert isinstance(win32_libs, list)
        assert len(win32_libs) > 0
        assert all(lib.endswith(".dll") for lib in win32_libs)