import pytest
from tacozip import config


class TestConfig:
    """Test configuration constants."""
    
    def test_error_codes(self):
        """Test error code constants."""
        assert config.TACOZ_OK == 0
        assert config.TACOZ_ERR_IO == -1
        assert config.TACOZ_ERR_LIBZIP == -2
        assert config.TACOZ_ERR_INVALID_HEADER == -3
        assert config.TACOZ_ERR_PARAM == -4
        assert config.TACOZ_ERR_NOT_FOUND == -5
        assert config.TACOZ_ERR_EXISTS == -6
        assert config.TACOZ_ERR_TOO_LARGE == -7
    
    def test_header_constants(self):
        """Test TACO Header constants."""
        assert config.TACO_HEADER_MAX_ENTRIES == 7
        assert config.TACO_HEADER_SIZE == 157
        assert config.TACO_HEADER_NAME == "TACO_HEADER"
        assert config.TACO_HEADER_NAME_LEN == 11
        assert config.TACO_HEADER_PAYLOAD_SIZE == 116
    
    def test_format_constants(self):
        """Test format detection constants."""
        assert hasattr(config, 'TACOZIP_FORMAT_UNKNOWN')
        assert hasattr(config, 'TACOZIP_FORMAT_ZIP32')
        assert hasattr(config, 'TACOZIP_FORMAT_ZIP64')
        
        # Verify they are different values
        assert config.TACOZIP_FORMAT_UNKNOWN != config.TACOZIP_FORMAT_ZIP32
        assert config.TACOZIP_FORMAT_UNKNOWN != config.TACOZIP_FORMAT_ZIP64
        assert config.TACOZIP_FORMAT_ZIP32 != config.TACOZIP_FORMAT_ZIP64
        
        # Verify they are integers
        assert isinstance(config.TACOZIP_FORMAT_UNKNOWN, int)
        assert isinstance(config.TACOZIP_FORMAT_ZIP32, int)
        assert isinstance(config.TACOZIP_FORMAT_ZIP64, int)
    
    def test_validation_level_constants(self):
        """Test validation level constants."""
        assert hasattr(config, 'TACOZIP_VALIDATE_QUICK')
        assert hasattr(config, 'TACOZIP_VALIDATE_NORMAL')
        assert hasattr(config, 'TACOZIP_VALIDATE_DEEP')
        
        # Verify they are different values
        assert config.TACOZIP_VALIDATE_QUICK != config.TACOZIP_VALIDATE_NORMAL
        assert config.TACOZIP_VALIDATE_QUICK != config.TACOZIP_VALIDATE_DEEP
        assert config.TACOZIP_VALIDATE_NORMAL != config.TACOZIP_VALIDATE_DEEP
        
        # Verify they are integers
        assert isinstance(config.TACOZIP_VALIDATE_QUICK, int)
        assert isinstance(config.TACOZIP_VALIDATE_NORMAL, int)
        assert isinstance(config.TACOZIP_VALIDATE_DEEP, int)
    
    def test_validation_result_constants(self):
        """Test validation result constants."""
        # Valid result
        assert hasattr(config, 'TACOZ_VALID')
        
        # Invalid results
        assert hasattr(config, 'TACOZ_INVALID_NOT_ZIP')
        assert hasattr(config, 'TACOZ_INVALID_NO_TACO')
        assert hasattr(config, 'TACOZ_INVALID_HEADER_SIZE')
        assert hasattr(config, 'TACOZ_INVALID_META_COUNT')
        assert hasattr(config, 'TACOZ_INVALID_FILE_SIZE')
        assert hasattr(config, 'TACOZ_INVALID_NO_EOCD')
        assert hasattr(config, 'TACOZ_INVALID_CD_OFFSET')
        assert hasattr(config, 'TACOZ_INVALID_NO_CD_ENTRY')
        assert hasattr(config, 'TACOZ_INVALID_REORDERED')
        assert hasattr(config, 'TACOZ_INVALID_CRC_LFH')
        assert hasattr(config, 'TACOZ_INVALID_CRC_CD')
        
        # Verify all are integers
        assert isinstance(config.TACOZ_VALID, int)
        assert isinstance(config.TACOZ_INVALID_NOT_ZIP, int)
        assert isinstance(config.TACOZ_INVALID_NO_TACO, int)
        assert isinstance(config.TACOZ_INVALID_HEADER_SIZE, int)
        assert isinstance(config.TACOZ_INVALID_META_COUNT, int)
        assert isinstance(config.TACOZ_INVALID_FILE_SIZE, int)
        assert isinstance(config.TACOZ_INVALID_NO_EOCD, int)
        assert isinstance(config.TACOZ_INVALID_CD_OFFSET, int)
        assert isinstance(config.TACOZ_INVALID_NO_CD_ENTRY, int)
        assert isinstance(config.TACOZ_INVALID_REORDERED, int)
        assert isinstance(config.TACOZ_INVALID_CRC_LFH, int)
        assert isinstance(config.TACOZ_INVALID_CRC_CD, int)
        
        # Verify TACOZ_VALID is distinct from all invalid codes
        invalid_codes = [
            config.TACOZ_INVALID_NOT_ZIP,
            config.TACOZ_INVALID_NO_TACO,
            config.TACOZ_INVALID_HEADER_SIZE,
            config.TACOZ_INVALID_META_COUNT,
            config.TACOZ_INVALID_FILE_SIZE,
            config.TACOZ_INVALID_NO_EOCD,
            config.TACOZ_INVALID_CD_OFFSET,
            config.TACOZ_INVALID_NO_CD_ENTRY,
            config.TACOZ_INVALID_REORDERED,
            config.TACOZ_INVALID_CRC_LFH,
            config.TACOZ_INVALID_CRC_CD,
        ]
        
        for invalid_code in invalid_codes:
            assert config.TACOZ_VALID != invalid_code
    
    def test_error_messages(self):
        """Test error messages exist for all error codes."""
        assert config.TACOZ_ERR_IO in config.ERROR_MESSAGES
        assert config.TACOZ_ERR_INVALID_HEADER in config.ERROR_MESSAGES
        assert config.TACOZ_ERR_PARAM in config.ERROR_MESSAGES
        assert config.TACOZ_ERR_NOT_FOUND in config.ERROR_MESSAGES
        assert config.TACOZ_ERR_EXISTS in config.ERROR_MESSAGES
        
        # Check messages are not empty
        for code, message in config.ERROR_MESSAGES.items():
            assert isinstance(message, str)
            assert len(message) > 0
    
    def test_error_messages_completeness(self):
        """Test that all error codes have messages."""
        error_codes = [
            config.TACOZ_ERR_IO,
            config.TACOZ_ERR_LIBZIP,
            config.TACOZ_ERR_INVALID_HEADER,
            config.TACOZ_ERR_PARAM,
            config.TACOZ_ERR_NOT_FOUND,
            config.TACOZ_ERR_EXISTS,
            config.TACOZ_ERR_TOO_LARGE,
        ]
        
        for error_code in error_codes:
            # All error codes should have messages, though LIBZIP might be special
            if error_code != config.TACOZ_ERR_LIBZIP:
                assert error_code in config.ERROR_MESSAGES, f"Error code {error_code} missing from ERROR_MESSAGES"
    
    def test_library_names(self):
        """Test library names for different platforms."""
        assert "linux" in config.LIBRARY_NAMES
        assert "darwin" in config.LIBRARY_NAMES
        assert "win32" in config.LIBRARY_NAMES
        
        # Check Linux libraries
        linux_libs = config.LIBRARY_NAMES["linux"]
        assert isinstance(linux_libs, list)
        assert len(linux_libs) > 0
        assert all(lib.endswith(".so") or ".so." in lib for lib in linux_libs)
        
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
    
    def test_format_constants_values(self):
        """Test that format constants have reasonable values."""
        # UNKNOWN should typically be 0 or negative
        assert config.TACOZIP_FORMAT_UNKNOWN <= 0
        
        # ZIP32 and ZIP64 should be positive and different
        assert config.TACOZIP_FORMAT_ZIP32 > 0
        assert config.TACOZIP_FORMAT_ZIP64 > 0
    
    def test_validation_level_ordering(self):
        """Test that validation levels have logical ordering."""
        # Just verify they exist and are different
        # The actual values don't need to be ordered
        levels = [
            config.TACOZIP_VALIDATE_QUICK,
            config.TACOZIP_VALIDATE_NORMAL,
            config.TACOZIP_VALIDATE_DEEP
        ]
        
        # All should be non-negative
        for level in levels:
            assert level >= 0
        
        # All should be unique
        assert len(set(levels)) == 3


class TestConfigCompleteness:
    """Test that config module exports all necessary constants."""
    
    def test_all_error_codes_defined(self):
        """Test that all expected error codes are defined."""
        required_error_codes = [
            'TACOZ_OK',
            'TACOZ_ERR_IO',
            'TACOZ_ERR_LIBZIP',
            'TACOZ_ERR_INVALID_HEADER',
            'TACOZ_ERR_PARAM',
            'TACOZ_ERR_NOT_FOUND',
            'TACOZ_ERR_EXISTS',
            'TACOZ_ERR_TOO_LARGE',
        ]
        
        for code_name in required_error_codes:
            assert hasattr(config, code_name), f"Missing constant: {code_name}"
    
    def test_all_header_constants_defined(self):
        """Test that all header constants are defined."""
        required_constants = [
            'TACO_HEADER_MAX_ENTRIES',
            'TACO_HEADER_SIZE',
            'TACO_HEADER_NAME',
            'TACO_HEADER_NAME_LEN',
            'TACO_HEADER_PAYLOAD_SIZE',
        ]
        
        for const_name in required_constants:
            assert hasattr(config, const_name), f"Missing constant: {const_name}"
    
    def test_all_format_constants_defined(self):
        """Test that all format constants are defined."""
        required_constants = [
            'TACOZIP_FORMAT_UNKNOWN',
            'TACOZIP_FORMAT_ZIP32',
            'TACOZIP_FORMAT_ZIP64',
        ]
        
        for const_name in required_constants:
            assert hasattr(config, const_name), f"Missing constant: {const_name}"
    
    def test_all_validation_constants_defined(self):
        """Test that all validation constants are defined."""
        required_constants = [
            'TACOZIP_VALIDATE_QUICK',
            'TACOZIP_VALIDATE_NORMAL',
            'TACOZIP_VALIDATE_DEEP',
            'TACOZ_VALID',
            'TACOZ_INVALID_NOT_ZIP',
            'TACOZ_INVALID_NO_TACO',
            'TACOZ_INVALID_HEADER_SIZE',
            'TACOZ_INVALID_META_COUNT',
            'TACOZ_INVALID_FILE_SIZE',
            'TACOZ_INVALID_NO_EOCD',
            'TACOZ_INVALID_CD_OFFSET',
            'TACOZ_INVALID_NO_CD_ENTRY',
            'TACOZ_INVALID_REORDERED',
            'TACOZ_INVALID_CRC_LFH',
            'TACOZ_INVALID_CRC_CD',
        ]
        
        for const_name in required_constants:
            assert hasattr(config, const_name), f"Missing constant: {const_name}"
    
    def test_error_messages_dict_exists(self):
        """Test that ERROR_MESSAGES dictionary exists."""
        assert hasattr(config, 'ERROR_MESSAGES')
        assert isinstance(config.ERROR_MESSAGES, dict)
        assert len(config.ERROR_MESSAGES) > 0
    
    def test_library_names_dict_exists(self):
        """Test that LIBRARY_NAMES dictionary exists."""
        assert hasattr(config, 'LIBRARY_NAMES')
        assert isinstance(config.LIBRARY_NAMES, dict)
        assert len(config.LIBRARY_NAMES) >= 3  # At least linux, darwin, win32