"""Test exception classes."""
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
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_INVALID_HEADER] in str(header_error)
        
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
        ]
        
        for error_code in error_codes:
            if error_code != config.TACOZ_ERR_LIBZIP:  # LIBZIP error might not have message
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