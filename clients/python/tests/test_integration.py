import pytest
import tacozip
from tacozip import config, exceptions


class TestIntegration:
    """Test package integration."""
    
    def test_package_imports(self):
        """Test that all expected items are importable from main package."""
        # Test constants
        assert hasattr(tacozip, 'TACOZ_OK')
        assert hasattr(tacozip, 'TACOZ_ERR_IO')
        assert hasattr(tacozip, 'TACOZ_ERR_INVALID_HEADER')
        assert hasattr(tacozip, 'TACOZ_ERR_EXISTS')
        assert hasattr(tacozip, 'TACO_HEADER_MAX_ENTRIES')
        
        # Test exception
        assert hasattr(tacozip, 'TacozipError')
        
        # Test functions
        assert hasattr(tacozip, 'create')
        assert hasattr(tacozip, 'read_header')
        assert hasattr(tacozip, 'update_header')
        assert hasattr(tacozip, 'append_files')
        assert hasattr(tacozip, 'replace_file')
        assert hasattr(tacozip, 'get_library_version')
        assert hasattr(tacozip, 'self_check')
        assert hasattr(tacozip, 'trim_from')  

        # Test metadata
        assert hasattr(tacozip, '__version__')
        assert hasattr(tacozip, '__tacozip_version__')
        assert hasattr(tacozip, '__author__')
        assert hasattr(tacozip, '__author_email__')
        assert hasattr(tacozip, '__description__')
        assert hasattr(tacozip, '__url__')
        assert hasattr(tacozip, '__license__')
    
    def test_constants_match(self):
        """Test that package constants match config constants."""
        assert tacozip.TACOZ_OK == config.TACOZ_OK
        assert tacozip.TACOZ_ERR_IO == config.TACOZ_ERR_IO
        assert tacozip.TACOZ_ERR_INVALID_HEADER == config.TACOZ_ERR_INVALID_HEADER
        assert tacozip.TACOZ_ERR_PARAM == config.TACOZ_ERR_PARAM
        assert tacozip.TACOZ_ERR_NOT_FOUND == config.TACOZ_ERR_NOT_FOUND
        assert tacozip.TACOZ_ERR_EXISTS == config.TACOZ_ERR_EXISTS
        assert tacozip.TACO_HEADER_MAX_ENTRIES == config.TACO_HEADER_MAX_ENTRIES
    
    def test_exception_accessibility(self):
        """Test that exceptions are accessible from main package."""
        assert tacozip.TacozipError is exceptions.TacozipError
        
        # Test that we can create exceptions
        exc = tacozip.TacozipError(-1, "test error")
        assert isinstance(exc, exceptions.TacozipError)
        assert exc.code == -1
        assert "test error" in str(exc)
    
    def test_all_exports(self):
        """Test that __all__ contains expected exports."""
        expected_exports = {
            # Version and metadata
            '__version__', '__tacozip_version__', '__author__', '__author_email__', 
            '__description__', '__url__', '__license__',
            
            # Loader
            'self_check',
            
            # Constants
            'TACOZ_OK', 'TACOZ_ERR_IO', 'TACOZ_ERR_LIBZIP', 'TACOZ_ERR_INVALID_HEADER',
            'TACOZ_ERR_PARAM', 'TACOZ_ERR_NOT_FOUND', 'TACOZ_ERR_EXISTS',
            'TACO_HEADER_MAX_ENTRIES',
            
            # Exceptions
            'TacozipError',
            
            # Core API
            'create', 'update_header', 'append_files', 'replace_file', 
            'read_header', 'get_library_version', 'trim_from'
        }
        
        actual_exports = set(tacozip.__all__)
        assert actual_exports == expected_exports
    
    def test_metadata_values(self):
        """Test package metadata values."""
        assert isinstance(tacozip.__version__, str)
        assert len(tacozip.__version__) > 0
        
        assert isinstance(tacozip.__tacozip_version__, str)
        assert len(tacozip.__tacozip_version__) > 0
        
        assert isinstance(tacozip.__author__, str)
        assert "Cesar Aybar" in tacozip.__author__
        
        assert isinstance(tacozip.__author_email__, str)
        assert "@" in tacozip.__author_email__
        
        assert isinstance(tacozip.__description__, str)
        assert "TACO" in tacozip.__description__
        assert "Header" in tacozip.__description__  # Should mention Header, not Ghost
        
        assert isinstance(tacozip.__url__, str)
        assert "github.com" in tacozip.__url__
        
        assert isinstance(tacozip.__license__, str)
        assert tacozip.__license__ == "MIT"
    
    def test_functions_callable(self):
        """Test that all exported functions are callable."""
        functions = [
            'create', 'read_header', 'update_header', 'append_files',
            'replace_file', 'get_library_version', 'self_check', 'trim_from'
        ]
        
        for func_name in functions:
            func = getattr(tacozip, func_name)
            assert callable(func), f"{func_name} should be callable"
    
    def test_error_code_completeness(self):
        """Test that all error codes are accessible from package."""
        error_codes = [
            'TACOZ_OK', 'TACOZ_ERR_IO', 'TACOZ_ERR_LIBZIP', 
            'TACOZ_ERR_INVALID_HEADER', 'TACOZ_ERR_PARAM', 
            'TACOZ_ERR_NOT_FOUND', 'TACOZ_ERR_EXISTS'
        ]
        
        for error_code in error_codes:
            assert hasattr(tacozip, error_code), f"{error_code} should be accessible"
            value = getattr(tacozip, error_code)
            assert isinstance(value, int), f"{error_code} should be an integer"
    
    def test_api_consistency(self):
        """Test that API functions have consistent patterns."""
        # Test that header functions exist (not ghost functions)
        assert hasattr(tacozip, 'read_header')
        assert hasattr(tacozip, 'update_header')
        
        # Test that old ghost functions do NOT exist
        assert not hasattr(tacozip, 'read_ghost')
        assert not hasattr(tacozip, 'update_ghost')
        
        # Test that non-existent multi functions do NOT exist
        assert not hasattr(tacozip, 'create_multi')
        assert not hasattr(tacozip, 'read_header_multi')
        assert not hasattr(tacozip, 'update_header_multi')
        
        # Test that actual functions exist
        assert hasattr(tacozip, 'append_files')
        assert hasattr(tacozip, 'get_library_version')