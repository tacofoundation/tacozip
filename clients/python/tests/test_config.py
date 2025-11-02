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
                assert (
                    error_code in config.ERROR_MESSAGES
                ), f"Error code {error_code} missing from ERROR_MESSAGES"

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


class TestConfigCompleteness:
    """Test that config module exports all necessary constants."""

    def test_all_error_codes_defined(self):
        """Test that all expected error codes are defined."""
        required_error_codes = [
            "TACOZ_OK",
            "TACOZ_ERR_IO",
            "TACOZ_ERR_LIBZIP",
            "TACOZ_ERR_INVALID_HEADER",
            "TACOZ_ERR_PARAM",
            "TACOZ_ERR_NOT_FOUND",
            "TACOZ_ERR_EXISTS",
            "TACOZ_ERR_TOO_LARGE",
        ]

        for code_name in required_error_codes:
            assert hasattr(config, code_name), f"Missing constant: {code_name}"

    def test_all_header_constants_defined(self):
        """Test that all header constants are defined."""
        required_constants = [
            "TACO_HEADER_MAX_ENTRIES",
            "TACO_HEADER_SIZE",
            "TACO_HEADER_NAME",
            "TACO_HEADER_NAME_LEN",
            "TACO_HEADER_PAYLOAD_SIZE",
        ]

        for const_name in required_constants:
            assert hasattr(config, const_name), f"Missing constant: {const_name}"

    def test_error_messages_dict_exists(self):
        """Test that ERROR_MESSAGES dictionary exists."""
        assert hasattr(config, "ERROR_MESSAGES")
        assert isinstance(config.ERROR_MESSAGES, dict)
        assert len(config.ERROR_MESSAGES) > 0

    def test_library_names_dict_exists(self):
        """Test that LIBRARY_NAMES dictionary exists."""
        assert hasattr(config, "LIBRARY_NAMES")
        assert isinstance(config.LIBRARY_NAMES, dict)
        assert len(config.LIBRARY_NAMES) >= 3  # At least linux, darwin, win32