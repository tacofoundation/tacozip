import pytest
import tacozip
from tacozip import config, exceptions


class TestIntegration:
    """Test package integration."""
    
    def test_package_imports(self):
        """Test that all expected items are importable from main package."""
        # Test error constants
        assert hasattr(tacozip, 'TACOZ_OK')
        assert hasattr(tacozip, 'TACOZ_ERR_IO')
        assert hasattr(tacozip, 'TACOZ_ERR_INVALID_HEADER')
        assert hasattr(tacozip, 'TACOZ_ERR_EXISTS')
        assert hasattr(tacozip, 'TACO_HEADER_MAX_ENTRIES')
        
        # Test format detection constants
        assert hasattr(tacozip, 'TACOZIP_FORMAT_UNKNOWN')
        assert hasattr(tacozip, 'TACOZIP_FORMAT_ZIP32')
        assert hasattr(tacozip, 'TACOZIP_FORMAT_ZIP64')
        
        # Test validation level constants
        assert hasattr(tacozip, 'TACOZIP_VALIDATE_QUICK')
        assert hasattr(tacozip, 'TACOZIP_VALIDATE_NORMAL')
        assert hasattr(tacozip, 'TACOZIP_VALIDATE_DEEP')
        
        # Test validation result constants
        assert hasattr(tacozip, 'TACOZ_VALID')
        assert hasattr(tacozip, 'TACOZ_INVALID_NOT_ZIP')
        assert hasattr(tacozip, 'TACOZ_INVALID_NO_TACO')
        assert hasattr(tacozip, 'TACOZ_INVALID_HEADER_SIZE')
        assert hasattr(tacozip, 'TACOZ_INVALID_META_COUNT')
        assert hasattr(tacozip, 'TACOZ_INVALID_FILE_SIZE')
        assert hasattr(tacozip, 'TACOZ_INVALID_NO_EOCD')
        assert hasattr(tacozip, 'TACOZ_INVALID_CD_OFFSET')
        assert hasattr(tacozip, 'TACOZ_INVALID_NO_CD_ENTRY')
        assert hasattr(tacozip, 'TACOZ_INVALID_REORDERED')
        assert hasattr(tacozip, 'TACOZ_INVALID_CRC_LFH')
        assert hasattr(tacozip, 'TACOZ_INVALID_CRC_CD')
        
        # Test exception
        assert hasattr(tacozip, 'TacozipError')
        
        # Test functions
        assert hasattr(tacozip, 'create')
        assert hasattr(tacozip, 'read_header')
        assert hasattr(tacozip, 'update_header')
        assert hasattr(tacozip, 'detect_format')
        assert hasattr(tacozip, 'validate')
        assert hasattr(tacozip, 'get_library_version')
        assert hasattr(tacozip, 'self_check')

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
        # Error codes
        assert tacozip.TACOZ_OK == config.TACOZ_OK
        assert tacozip.TACOZ_ERR_IO == config.TACOZ_ERR_IO
        assert tacozip.TACOZ_ERR_INVALID_HEADER == config.TACOZ_ERR_INVALID_HEADER
        assert tacozip.TACOZ_ERR_PARAM == config.TACOZ_ERR_PARAM
        assert tacozip.TACOZ_ERR_NOT_FOUND == config.TACOZ_ERR_NOT_FOUND
        assert tacozip.TACOZ_ERR_EXISTS == config.TACOZ_ERR_EXISTS
        assert tacozip.TACO_HEADER_MAX_ENTRIES == config.TACO_HEADER_MAX_ENTRIES
        
        # Format constants
        assert tacozip.TACOZIP_FORMAT_UNKNOWN == config.TACOZIP_FORMAT_UNKNOWN
        assert tacozip.TACOZIP_FORMAT_ZIP32 == config.TACOZIP_FORMAT_ZIP32
        assert tacozip.TACOZIP_FORMAT_ZIP64 == config.TACOZIP_FORMAT_ZIP64
        
        # Validation level constants
        assert tacozip.TACOZIP_VALIDATE_QUICK == config.TACOZIP_VALIDATE_QUICK
        assert tacozip.TACOZIP_VALIDATE_NORMAL == config.TACOZIP_VALIDATE_NORMAL
        assert tacozip.TACOZIP_VALIDATE_DEEP == config.TACOZIP_VALIDATE_DEEP
        
        # Validation result constants
        assert tacozip.TACOZ_VALID == config.TACOZ_VALID
        assert tacozip.TACOZ_INVALID_NOT_ZIP == config.TACOZ_INVALID_NOT_ZIP
        assert tacozip.TACOZ_INVALID_NO_TACO == config.TACOZ_INVALID_NO_TACO
        assert tacozip.TACOZ_INVALID_HEADER_SIZE == config.TACOZ_INVALID_HEADER_SIZE
        assert tacozip.TACOZ_INVALID_META_COUNT == config.TACOZ_INVALID_META_COUNT
        assert tacozip.TACOZ_INVALID_FILE_SIZE == config.TACOZ_INVALID_FILE_SIZE
        assert tacozip.TACOZ_INVALID_NO_EOCD == config.TACOZ_INVALID_NO_EOCD
        assert tacozip.TACOZ_INVALID_CD_OFFSET == config.TACOZ_INVALID_CD_OFFSET
        assert tacozip.TACOZ_INVALID_NO_CD_ENTRY == config.TACOZ_INVALID_NO_CD_ENTRY
        assert tacozip.TACOZ_INVALID_REORDERED == config.TACOZ_INVALID_REORDERED
        assert tacozip.TACOZ_INVALID_CRC_LFH == config.TACOZ_INVALID_CRC_LFH
        assert tacozip.TACOZ_INVALID_CRC_CD == config.TACOZ_INVALID_CRC_CD
    
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
            
            # Error codes
            'TACOZ_OK', 'TACOZ_ERR_IO', 'TACOZ_ERR_LIBZIP', 'TACOZ_ERR_INVALID_HEADER',
            'TACOZ_ERR_PARAM', 'TACOZ_ERR_NOT_FOUND', 'TACOZ_ERR_EXISTS', 'TACOZ_ERR_TOO_LARGE',
            'TACO_HEADER_MAX_ENTRIES',
            
            # Format detection
            'TACOZIP_FORMAT_UNKNOWN', 'TACOZIP_FORMAT_ZIP32', 'TACOZIP_FORMAT_ZIP64',
            
            # Validation levels
            'TACOZIP_VALIDATE_QUICK', 'TACOZIP_VALIDATE_NORMAL', 'TACOZIP_VALIDATE_DEEP',
            
            # Validation results
            'TACOZ_VALID', 'TACOZ_INVALID_NOT_ZIP', 'TACOZ_INVALID_NO_TACO',
            'TACOZ_INVALID_HEADER_SIZE', 'TACOZ_INVALID_META_COUNT', 'TACOZ_INVALID_FILE_SIZE',
            'TACOZ_INVALID_NO_EOCD', 'TACOZ_INVALID_CD_OFFSET', 'TACOZ_INVALID_NO_CD_ENTRY',
            'TACOZ_INVALID_REORDERED', 'TACOZ_INVALID_CRC_LFH', 'TACOZ_INVALID_CRC_CD',
            
            # Exceptions
            'TacozipError',
            
            # Core API
            'create', 'update_header', 'read_header', 
            'detect_format', 'validate', 'get_library_version'
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
        assert "Header" in tacozip.__description__
        
        assert isinstance(tacozip.__url__, str)
        assert "github.com" in tacozip.__url__
        
        assert isinstance(tacozip.__license__, str)
        assert tacozip.__license__ == "MIT"
    
    def test_functions_callable(self):
        """Test that all exported functions are callable."""
        functions = [
            'create', 'read_header', 'update_header',
            'detect_format', 'validate',
            'get_library_version', 'self_check'
        ]
        
        for func_name in functions:
            func = getattr(tacozip, func_name)
            assert callable(func), f"{func_name} should be callable"
    
    def test_error_code_completeness(self):
        """Test that all error codes are accessible from package."""
        error_codes = [
            'TACOZ_OK', 'TACOZ_ERR_IO', 'TACOZ_ERR_LIBZIP', 
            'TACOZ_ERR_INVALID_HEADER', 'TACOZ_ERR_PARAM', 
            'TACOZ_ERR_NOT_FOUND', 'TACOZ_ERR_EXISTS', 'TACOZ_ERR_TOO_LARGE'
        ]
        
        for error_code in error_codes:
            assert hasattr(tacozip, error_code), f"{error_code} should be accessible"
            value = getattr(tacozip, error_code)
            assert isinstance(value, int), f"{error_code} should be an integer"
    
    def test_format_constants_completeness(self):
        """Test that all format constants are accessible from package."""
        format_constants = [
            'TACOZIP_FORMAT_UNKNOWN',
            'TACOZIP_FORMAT_ZIP32',
            'TACOZIP_FORMAT_ZIP64'
        ]
        
        for const_name in format_constants:
            assert hasattr(tacozip, const_name), f"{const_name} should be accessible"
            value = getattr(tacozip, const_name)
            assert isinstance(value, int), f"{const_name} should be an integer"
    
    def test_validation_constants_completeness(self):
        """Test that all validation constants are accessible from package."""
        validation_constants = [
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
            'TACOZ_INVALID_CRC_CD'
        ]
        
        for const_name in validation_constants:
            assert hasattr(tacozip, const_name), f"{const_name} should be accessible"
            value = getattr(tacozip, const_name)
            assert isinstance(value, int), f"{const_name} should be an integer"
    
    def test_api_consistency(self):
        """Test that API functions have consistent patterns."""
        # Test that header functions exist
        assert hasattr(tacozip, 'read_header')
        assert hasattr(tacozip, 'update_header')
        
        # Test that new functions exist
        assert hasattr(tacozip, 'detect_format')
        assert hasattr(tacozip, 'validate')
        
        # Test that removed functions do NOT exist
        assert not hasattr(tacozip, 'append_files')
        assert not hasattr(tacozip, 'trim_from')
        
        # Test that old ghost functions do NOT exist
        assert not hasattr(tacozip, 'read_ghost')
        assert not hasattr(tacozip, 'update_ghost')
        
        # Test that non-existent multi functions do NOT exist
        assert not hasattr(tacozip, 'create_multi')
        assert not hasattr(tacozip, 'read_header_multi')
        assert not hasattr(tacozip, 'update_header_multi')
    
    def test_constants_types(self):
        """Test that constants have correct types."""
        # Integer constants
        int_constants = [
            'TACOZ_OK', 'TACOZ_ERR_IO', 'TACO_HEADER_MAX_ENTRIES',
            'TACOZIP_FORMAT_ZIP32', 'TACOZIP_FORMAT_ZIP64',
            'TACOZIP_VALIDATE_QUICK', 'TACOZIP_VALIDATE_NORMAL',
            'TACOZ_VALID', 'TACOZ_INVALID_NOT_ZIP'
        ]
        
        for const_name in int_constants:
            value = getattr(tacozip, const_name)
            assert isinstance(value, int), f"{const_name} should be int"
        
        # String metadata
        string_metadata = [
            '__version__', '__tacozip_version__', '__author__',
            '__author_email__', '__description__', '__url__', '__license__'
        ]
        
        for meta_name in string_metadata:
            value = getattr(tacozip, meta_name)
            assert isinstance(value, str), f"{meta_name} should be str"


class TestImportStructure:
    """Test import structure and dependencies."""
    
    def test_submodules_importable(self):
        """Test that submodules can be imported."""
        from tacozip import config
        from tacozip import exceptions
        from tacozip import bindings
        from tacozip import loader
        from tacozip import version
        
        # Verify they are modules
        assert config is not None
        assert exceptions is not None
        assert bindings is not None
        assert loader is not None
        assert version is not None
    
    def test_config_importable(self):
        """Test that config module is importable."""
        from tacozip import config
        
        # Config should have expected attributes
        assert hasattr(config, 'TACOZ_OK')
        assert hasattr(config, 'TACO_HEADER_MAX_ENTRIES')
    
    def test_exceptions_importable(self):
        """Test that exceptions module is importable."""
        from tacozip import exceptions
        
        # Exceptions should have TacozipError
        assert hasattr(exceptions, 'TacozipError')
    
    def test_no_circular_imports(self):
        """Test that there are no circular import issues."""
        # This test passes if the imports complete without error
        import tacozip
        from tacozip import config
        from tacozip import exceptions
        from tacozip import bindings
        from tacozip import loader
        
        # All should import successfully
        assert tacozip is not None
        assert config is not None
        assert exceptions is not None
        assert bindings is not None
        assert loader is not None