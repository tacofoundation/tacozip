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
    
    def test_tacozip_error_with_known_code(self):
        """Test TacozipError with known error code."""
        error = exceptions.TacozipError(config.TACOZ_ERR_IO)
        assert error.code == config.TACOZ_ERR_IO
        assert config.ERROR_MESSAGES[config.TACOZ_ERR_IO] in str(error)
    
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
