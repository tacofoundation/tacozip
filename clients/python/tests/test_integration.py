"""Test package integration."""
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
        assert hasattr(tacozip, 'TACO_GHOST_MAX_ENTRIES')
        
        # Test exception
        assert hasattr(tacozip, 'TacozipError')
        
        # Test functions
        assert hasattr(tacozip, 'create')
        assert hasattr(tacozip, 'read_ghost')
        assert hasattr(tacozip, 'update_ghost')
        assert hasattr(tacozip, 'create_multi')
        assert hasattr(tacozip, 'read_ghost_multi')
        assert hasattr(tacozip, 'update_ghost_multi')
        assert hasattr(tacozip, 'replace_file')
        assert hasattr(tacozip, 'self_check')
        
        # Test metadata
        assert hasattr(tacozip, '__version__')
        assert hasattr(tacozip, '__author__')
        assert hasattr(tacozip, '__author_email__')
        assert hasattr(tacozip, '__description__')
        assert hasattr(tacozip, '__url__')
        assert hasattr(tacozip, '__license__')
    
    def test_constants_match(self):
        """Test that package constants match config constants."""
        assert tacozip.TACOZ_OK == config.TACOZ_OK
        assert tacozip.TACOZ_ERR_IO == config.TACOZ_ERR_IO
        assert tacozip.TACOZ_ERR_INVALID_GHOST == config.TACOZ_ERR_INVALID_GHOST
        assert tacozip.TACOZ_ERR_PARAM == config.TACOZ_ERR_PARAM
        assert tacozip.TACOZ_ERR_NOT_FOUND == config.TACOZ_ERR_NOT_FOUND
        assert tacozip.TACO_GHOST_MAX_ENTRIES == config.TACO_GHOST_MAX_ENTRIES
    
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
            '__version__', '__author__', '__author_email__', '__description__',
            '__url__', '__license__', 'self_check', 'TACOZ_OK', 'TACOZ_ERR_IO',
            'TACOZ_ERR_LIBZIP', 'TACOZ_ERR_INVALID_GHOST', 'TACOZ_ERR_PARAM',
            'TACOZ_ERR_NOT_FOUND', 'TACO_GHOST_MAX_ENTRIES', 'TacozipError',
            'create', 'read_ghost', 'update_ghost', 'create_multi',
            'read_ghost_multi', 'update_ghost_multi', 'replace_file'
        }
        
        actual_exports = set(tacozip.__all__)
        assert actual_exports == expected_exports
    
    def test_metadata_values(self):
        """Test package metadata values."""
        assert isinstance(tacozip.__version__, str)
        assert len(tacozip.__version__) > 0
        
        assert isinstance(tacozip.__author__, str)
        assert "Cesar Aybar" in tacozip.__author__
        
        assert isinstance(tacozip.__author_email__, str)
        assert "@" in tacozip.__author_email__
        
        assert isinstance(tacozip.__description__, str)
        assert "TACO" in tacozip.__description__
        
        assert isinstance(tacozip.__url__, str)
        assert "github.com" in tacozip.__url__
        
        assert isinstance(tacozip.__license__, str)
        assert tacozip.__license__ == "MIT"
    
    def test_functions_callable(self):
        """Test that all exported functions are callable."""
        functions = [
            'create', 'read_ghost', 'update_ghost', 'create_multi',
            'read_ghost_multi', 'update_ghost_multi', 'replace_file', 'self_check'
        ]
        
        for func_name in functions:
            func = getattr(tacozip, func_name)
            assert callable(func), f"{func_name} should be callable"
