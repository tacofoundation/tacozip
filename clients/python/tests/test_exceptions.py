import pytest
from tacozip import exceptions, config


class TestExceptions:
    """Test exception classes."""

    def test_tacozip_error_basic(self):
        """Test basic TacozipError functionality."""
        error = exceptions.TacozipError(-1, "Test error")
        assert error.code == -1
        assert "Test error" in str(error)
        assert "-1" in str(error)

    def test_tacozip_error_with_known_codes(self):
        """Test TacozipError with all known error codes."""
        # Test IO error
        io_error = exceptions.TacozipError(config.TACOZ_ERR_IO)
        assert io_error.code == config.TACOZ_ERR_IO
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_IO] in str(io_error)

        # Test invalid header error
        header_error = exceptions.TacozipError(config.TACOZ_ERR_INVALID_HEADER)
        assert header_error.code == config.TACOZ_ERR_INVALID_HEADER
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_INVALID_HEADER] in str(
            header_error
        )

        # Test parameter error
        param_error = exceptions.TacozipError(config.TACOZ_ERR_PARAM)
        assert param_error.code == config.TACOZ_ERR_PARAM
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_PARAM] in str(param_error)

        # Test not found error
        not_found_error = exceptions.TacozipError(config.TACOZ_ERR_NOT_FOUND)
        assert not_found_error.code == config.TACOZ_ERR_NOT_FOUND
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_NOT_FOUND] in str(not_found_error)

        # Test exists error
        exists_error = exceptions.TacozipError(config.TACOZ_ERR_EXISTS)
        assert exists_error.code == config.TACOZ_ERR_EXISTS
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_EXISTS] in str(exists_error)

        # Test too large error
        too_large_error = exceptions.TacozipError(config.TACOZ_ERR_TOO_LARGE)
        assert too_large_error.code == config.TACOZ_ERR_TOO_LARGE
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_TOO_LARGE] in str(too_large_error)

    def test_tacozip_error_with_unknown_code(self):
        """Test TacozipError with unknown error code."""
        error = exceptions.TacozipError(-999)
        assert error.code == -999
        assert "Unknown error code: -999" in str(error)

    def test_tacozip_error_inheritance(self):
        """Test that TacozipError inherits from Exception."""
        error = exceptions.TacozipError(-1, "Test")
        assert isinstance(error, Exception)

    def test_specialized_exceptions(self):
        """Test specialized exception classes."""
        # Test TacozipIOError
        io_error = exceptions.TacozipIOError(-1, "IO error")
        assert isinstance(io_error, exceptions.TacozipError)
        assert isinstance(io_error, Exception)
        assert "IO error" in str(io_error)

        # Test TacozipValidationError
        validation_error = exceptions.TacozipValidationError(-4, "Validation error")
        assert isinstance(validation_error, exceptions.TacozipError)
        assert isinstance(validation_error, Exception)
        assert "Validation error" in str(validation_error)

        # Test TacozipLibraryError
        library_error = exceptions.TacozipLibraryError(-1, "Library error")
        assert isinstance(library_error, exceptions.TacozipError)
        assert isinstance(library_error, Exception)
        assert "Library error" in str(library_error)

    def test_error_code_coverage(self):
        """Test that all defined error codes have corresponding messages."""
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
            if (
                error_code != config.TACOZ_ERR_LIBZIP
            ):  # LIBZIP error might not have message
                assert error_code in config.ERROR_MESSAGES
                error = exceptions.TacozipError(error_code)
                assert config.ERROR_MESSAGES[error_code] in str(error)

    def test_custom_message_override(self):
        """Test that custom messages override default messages."""
        custom_message = "Custom error message"
        error = exceptions.TacozipError(config.TACOZ_ERR_IO, custom_message)
        assert error.code == config.TACOZ_ERR_IO
        assert custom_message in str(error)
        # Custom message should override the default one
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_IO] not in str(error)

    def test_error_code_attribute(self):
        """Test that error code is accessible as attribute."""
        error = exceptions.TacozipError(config.TACOZ_ERR_PARAM, "Parameter error")
        assert hasattr(error, "code")
        assert error.code == config.TACOZ_ERR_PARAM

    def test_error_with_no_message(self):
        """Test error creation with only error code."""
        error = exceptions.TacozipError(config.TACOZ_ERR_IO)
        assert error.code == config.TACOZ_ERR_IO
        # Should use default message from ERROR_MESSAGES
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_IO] in str(error)

    def test_error_string_representation(self):
        """Test string representation of errors."""
        error = exceptions.TacozipError(-5, "File not found")
        error_str = str(error)

        # Should contain both code and message
        assert "-5" in error_str
        assert "File not found" in error_str


class TestExceptionHandling:
    """Test exception handling patterns."""

    def test_catch_base_exception(self):
        """Test that TacozipError can be caught as Exception."""
        try:
            raise exceptions.TacozipError(-1, "Test error")
        except Exception as e:
            assert isinstance(e, exceptions.TacozipError)
            assert e.code == -1

    def test_catch_specific_exception(self):
        """Test that TacozipError can be caught specifically."""
        try:
            raise exceptions.TacozipError(-1, "Test error")
        except exceptions.TacozipError as e:
            assert e.code == -1
            assert "Test error" in str(e)

    def test_catch_specialized_exceptions(self):
        """Test that specialized exceptions can be caught."""
        # IO Error
        try:
            raise exceptions.TacozipIOError(-1, "IO failed")
        except exceptions.TacozipIOError as e:
            assert "IO failed" in str(e)

        # Validation Error
        try:
            raise exceptions.TacozipValidationError(-4, "Validation failed")
        except exceptions.TacozipValidationError as e:
            assert "Validation failed" in str(e)

        # Library Error
        try:
            raise exceptions.TacozipLibraryError(-2, "Library error")
        except exceptions.TacozipLibraryError as e:
            assert "Library error" in str(e)

    def test_exception_chaining(self):
        """Test exception chaining with from clause."""
        original_error = ValueError("Original error")

        try:
            raise exceptions.TacozipError(-1, "Wrapped error") from original_error
        except exceptions.TacozipError as e:
            assert e.code == -1
            assert e.__cause__ is original_error


class TestExceptionEdgeCases:
    """Test edge cases for exception handling."""

    def test_error_with_empty_message(self):
        """Test error with empty string message."""
        error = exceptions.TacozipError(-1, "")
        assert error.code == -1
        # Should still have error code in representation
        assert "-1" in str(error)

    def test_error_with_none_message(self):
        """Test error with None message."""
        error = exceptions.TacozipError(-1, None)
        assert error.code == -1
        # Should handle None gracefully
        str(error)  # Should not raise

    def test_error_with_zero_code(self):
        """Test error with zero code (TACOZ_OK)."""
        # This is unusual but should work
        error = exceptions.TacozipError(0, "Success code as error")
        assert error.code == 0

    def test_error_with_large_code(self):
        """Test error with very large error code."""
        error = exceptions.TacozipError(-999999, "Large code")
        assert error.code == -999999
        assert "-999999" in str(error)

    def test_error_repr(self):
        """Test __repr__ method if implemented."""
        error = exceptions.TacozipError(-1, "Test")
        repr_str = repr(error)
        # repr should be informative
        assert repr_str is not None
        assert isinstance(repr_str, str)


class TestExceptionMessages:
    """Test exception message formatting."""

    def test_message_with_special_characters(self):
        """Test error messages with special characters."""
        special_chars = "Error: file 'test.txt' not found!"
        error = exceptions.TacozipError(-1, special_chars)
        assert special_chars in str(error)

    def test_message_with_unicode(self):
        """Test error messages with unicode characters."""
        unicode_msg = "Archivo no encontrado: test_ñ.txt"
        error = exceptions.TacozipError(-1, unicode_msg)
        assert unicode_msg in str(error)

    def test_message_with_newlines(self):
        """Test error messages with newlines."""
        multiline_msg = "Error occurred:\nLine 1\nLine 2"
        error = exceptions.TacozipError(-1, multiline_msg)
        assert "Line 1" in str(error)
        assert "Line 2" in str(error)

    def test_long_message(self):
        """Test error with very long message."""
        long_msg = "x" * 1000
        error = exceptions.TacozipError(-1, long_msg)
        assert long_msg in str(error)


class TestExceptionContext:
    """Test exception usage in different contexts."""

    def test_error_in_with_statement(self):
        """Test that errors work in with statements."""

        class ContextManager:
            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc_val, exc_tb):
                if exc_type is exceptions.TacozipError:
                    assert exc_val.code == -1
                    return True
                return False

        with ContextManager():
            raise exceptions.TacozipError(-1, "Context test")

    def test_error_attributes_accessible(self):
        """Test that error attributes are accessible after catch."""
        caught_error = None

        try:
            raise exceptions.TacozipError(-5, "Not found")
        except exceptions.TacozipError as e:
            caught_error = e

        assert caught_error is not None
        assert caught_error.code == -5
        assert "Not found" in str(caught_error)
