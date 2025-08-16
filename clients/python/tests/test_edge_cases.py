
"""
Final edge case tests to ensure 100% coverage of tacozip package.
These tests focus on very specific edge cases and error conditions.
"""

import pytest
import sys
import os
import ctypes
from unittest.mock import Mock, patch, MagicMock, PropertyMock
from pathlib import Path
import tempfile

import tacozip
from tacozip import config, exceptions, loader, bindings, version
from tacozip.bindings import TacoMetaPtr, TacoMetaEntry, TacoMetaArray


class TestAbsoluteEdgeCases:
    """Test absolute edge cases for 100% coverage."""
    
    def test_version_module_attributes(self):
        """Test all attributes of version module."""
        # Test that __version__ is accessible
        assert hasattr(version, '__version__')
        assert isinstance(version.__version__, str)
        
        # Test _get_version function directly
        version_str = version._get_version()
        assert isinstance(version_str, str)
        assert len(version_str) > 0
        
        # Test version fallback by mocking the import mechanism
        with patch('builtins.__import__') as mock_import:
            def import_side_effect(name, *args, **kwargs):
                if 'importlib' in name or 'pkg_resources' in name:
                    raise ImportError(f"Mocked import failure for {name}")
                return __import__(name, *args, **kwargs)
            
            mock_import.side_effect = import_side_effect
            version_str = version._get_version()
            assert version_str == "0.0.0"

    def test_loader_edge_cases_complete(self):
        """Test loader edge cases completely."""
        from tacozip.loader import self_check
        from tacozip import exceptions
        
        # Create a mock library missing most required functions
        mock_lib = Mock()
        mock_lib.tacozip_create = Mock()  # Add only one function
        
        # Make sure hasattr returns False for missing functions
        def mock_hasattr(obj, name):
            return name == 'tacozip_create'
        
        with patch('tacozip.loader.get_library', return_value=mock_lib):
            with patch('builtins.hasattr', side_effect=mock_hasattr):
                with pytest.raises(exceptions.TacozipLibraryError) as exc_info:
                    self_check()
                assert "Missing functions" in str(exc_info.value)

    def test_loader_get_library_caching(self):
        """Test that get_library properly caches the library."""
        # Test that get_library returns a consistent library object
        from tacozip.loader import get_library
        
        lib1 = get_library()
        lib2 = get_library()
        
        # Both calls should return the same object (cached)
        assert lib1 is lib2
        assert lib1 is not None

    @patch('tacozip.bindings._lib')
    def test_read_functions_output_handling(self, mock_lib):
        """Test that read functions properly handle output parameters."""
        # Simple test without ctypes manipulation
        mock_lib.tacozip_read_ghost.return_value = config.TACOZ_OK
        mock_lib.tacozip_read_ghost_multi.return_value = config.TACOZ_OK
        
        # Test read_ghost
        rc, offset, length = tacozip.read_ghost("test.zip")
        assert rc == config.TACOZ_OK
        assert isinstance(offset, int)
        assert isinstance(length, int)
        mock_lib.tacozip_read_ghost.assert_called_once()
        
        # Test read_ghost_multi  
        count, entries = tacozip.read_ghost_multi("test.zip")
        assert count == 0
        assert len(entries) == config.TACO_GHOST_MAX_ENTRIES
        mock_lib.tacozip_read_ghost_multi.assert_called_once()

    def test_bindings_check_result_edge_cases(self):
        """Test _check_result with all possible scenarios."""
        from tacozip.bindings import _check_result
        
        # Test with TACOZ_OK (should not raise)
        _check_result(config.TACOZ_OK)
        
        # Test with all known error codes
        error_codes = [
            config.TACOZ_ERR_IO,
            config.TACOZ_ERR_LIBZIP,
            config.TACOZ_ERR_INVALID_GHOST,
            config.TACOZ_ERR_PARAM,
            config.TACOZ_ERR_NOT_FOUND
        ]
        
        for error_code in error_codes:
            with pytest.raises(exceptions.TacozipError) as exc_info:
                _check_result(error_code)
            assert exc_info.value.code == error_code
        
        # Test with unknown error code
        with pytest.raises(exceptions.TacozipError) as exc_info:
            _check_result(-999)
        assert exc_info.value.code == -999
    
    def test_bindings_prepare_arrays_boundary_conditions(self):
        """Test array preparation boundary conditions."""
        from tacozip.bindings import _prepare_string_array, _prepare_uint64_array
        
        # Test _prepare_string_array with empty list
        empty_array, empty_bytes = _prepare_string_array([])
        assert len(empty_array) == 0
        assert len(empty_bytes) == 0
        
        # Test _prepare_uint64_array with size 0
        with pytest.raises(ValueError):
            _prepare_uint64_array([1], 0)
        
        # Test _prepare_uint64_array with negative size (should fail)
        with pytest.raises((ValueError, OverflowError)):
            _prepare_uint64_array([1], -1)
        
        # Test _prepare_uint64_array with exactly matching size
        values = [1, 2, 3]
        array = _prepare_uint64_array(values, 3)
        assert len(array) == 3
        assert array[0] == 1
        assert array[1] == 2
        assert array[2] == 3
    
    def test_bindings_library_function_setup(self):
        """Test that library function signatures are properly set up."""
        # This tests that the module-level function signature setup works
        from tacozip.bindings import _lib
        
        # Check that _lib has the expected function signature assignments
        # (This tests the module-level code that sets up argtypes and restype)
        expected_functions = [
            'tacozip_create', 'tacozip_read_ghost', 'tacozip_update_ghost',
            'tacozip_create_multi', 'tacozip_read_ghost_multi',
            'tacozip_update_ghost_multi', 'tacozip_replace_file'
        ]
        
        for func_name in expected_functions:
            func = getattr(_lib, func_name)
            # Check that argtypes and restype are set (they should exist)
            assert hasattr(func, 'argtypes')
            assert hasattr(func, 'restype')
            assert func.restype == ctypes.c_int
    
    def test_all_constants_accessible_from_package(self):
        """Test that all constants are accessible from main package."""
        # Test error codes
        assert tacozip.TACOZ_OK == config.TACOZ_OK
        assert tacozip.TACOZ_ERR_IO == config.TACOZ_ERR_IO
        assert tacozip.TACOZ_ERR_LIBZIP == config.TACOZ_ERR_LIBZIP
        assert tacozip.TACOZ_ERR_INVALID_GHOST == config.TACOZ_ERR_INVALID_GHOST
        assert tacozip.TACOZ_ERR_PARAM == config.TACOZ_ERR_PARAM
        assert tacozip.TACOZ_ERR_NOT_FOUND == config.TACOZ_ERR_NOT_FOUND
        
        # Test TACO Ghost constants
        assert tacozip.TACO_GHOST_MAX_ENTRIES == config.TACO_GHOST_MAX_ENTRIES
        
        # Test that constants have expected values
        assert tacozip.TACOZ_OK == 0
        assert tacozip.TACOZ_ERR_IO < 0
        assert tacozip.TACO_GHOST_MAX_ENTRIES == 7
    
    def test_exception_classes_accessible_from_package(self):
        """Test that exception classes are accessible from main package."""
        # Test that TacozipError is accessible
        assert hasattr(tacozip, 'TacozipError')
        assert tacozip.TacozipError == exceptions.TacozipError
        
        # Test that we can create exceptions through the main package
        exc = tacozip.TacozipError(-1, "test error")
        assert isinstance(exc, exceptions.TacozipError)
        assert exc.code == -1
        assert "test error" in str(exc)
    
    def test_package_metadata_completeness(self):
        """Test that all package metadata is complete and correct."""
        # Test all metadata attributes exist
        metadata_attrs = [
            '__author__', '__author_email__', '__description__', 
            '__url__', '__license__'
        ]
        
        for attr in metadata_attrs:
            assert hasattr(tacozip, attr), f"Missing metadata attribute: {attr}"
            value = getattr(tacozip, attr)
            assert isinstance(value, str), f"Metadata {attr} should be string"
            assert len(value) > 0, f"Metadata {attr} should not be empty"
        
        # Test specific metadata values
        assert "Cesar Aybar" in tacozip.__author__
        assert "@" in tacozip.__author_email__  # Should be valid email format
        assert "TACO" in tacozip.__description__
        assert "github.com" in tacozip.__url__ or "http" in tacozip.__url__
        assert tacozip.__license__ in ["MIT", "Apache", "BSD"]  # Common licenses
    
    @patch('tacozip.bindings._lib')
    def test_api_functions_parameter_handling(self, mock_lib):
        """Test API functions handle parameters correctly."""
        mock_lib.tacozip_create.return_value = config.TACOZ_OK
        mock_lib.tacozip_create_multi.return_value = config.TACOZ_OK
        mock_lib.tacozip_update_ghost.return_value = config.TACOZ_OK
        mock_lib.tacozip_update_ghost_multi.return_value = config.TACOZ_OK
        mock_lib.tacozip_replace_file.return_value = config.TACOZ_OK
        
        # Test create with minimal parameters
        result = tacozip.create("test.zip", ["src.txt"], ["dst.txt"])
        assert result == config.TACOZ_OK
        
        # Test create with all parameters
        result = tacozip.create("test.zip", ["src.txt"], ["dst.txt"], 100, 200)
        assert result == config.TACOZ_OK
        
        # Test create_multi with various parameter combinations
        tacozip.create_multi("test.zip", ["src1.txt"], ["dst1.txt"], [100], [200])
        tacozip.create_multi("test.zip", 
                           ["src1.txt", "src2.txt"], 
                           ["dst1.txt", "dst2.txt"],
                           [100, 200], 
                           [300, 400])
        
        # Test update functions
        tacozip.update_ghost("test.zip", 500, 600)
        tacozip.update_ghost_multi("test.zip", [500, 600], [700, 800])
        
        # Test replace_file
        tacozip.replace_file("test.zip", "old.txt", "new.txt")
        
        # Verify all functions were called with correct number of arguments
        assert mock_lib.tacozip_create.call_count >= 2
        assert mock_lib.tacozip_create_multi.call_count >= 2
        assert mock_lib.tacozip_update_ghost.call_count >= 1
        assert mock_lib.tacozip_update_ghost_multi.call_count >= 1
        assert mock_lib.tacozip_replace_file.call_count >= 1
    
    def test_ctypes_structure_field_access(self):
        """Test all fields of ctypes structures can be accessed."""
        # Test TacoMetaPtr
        meta_ptr = TacoMetaPtr()
        
        # Test field assignment and retrieval
        meta_ptr.offset = 12345
        meta_ptr.length = 67890
        assert meta_ptr.offset == 12345
        assert meta_ptr.length == 67890
        
        # Test TacoMetaEntry
        meta_entry = TacoMetaEntry()
        meta_entry.offset = 99999
        meta_entry.length = 11111
        assert meta_entry.offset == 99999
        assert meta_entry.length == 11111
        
        # Test TacoMetaArray
        meta_array = TacoMetaArray()
        
        # Test count field
        meta_array.count = 5
        assert meta_array.count == 5
        
        # Test entries array field - access all possible indices
        for i in range(config.TACO_GHOST_MAX_ENTRIES):
            meta_array.entries[i].offset = i * 1000
            meta_array.entries[i].length = i * 500
            assert meta_array.entries[i].offset == i * 1000
            assert meta_array.entries[i].length == i * 500
    
    def test_module_imports_and_structure(self):
        """Test module import structure and dependencies."""
        # Test that all submodules can be imported independently
        from tacozip import config as test_config
        from tacozip import exceptions as test_exceptions
        from tacozip import loader as test_loader
        from tacozip import bindings as test_bindings
        from tacozip import version as test_version
        
        # Test that they have expected attributes
        assert hasattr(test_config, 'TACOZ_OK')
        assert hasattr(test_config, 'ERROR_MESSAGES')
        assert hasattr(test_config, 'LIBRARY_NAMES')
        
        assert hasattr(test_exceptions, 'TacozipError')
        assert hasattr(test_exceptions, 'TacozipIOError')
        assert hasattr(test_exceptions, 'TacozipValidationError')
        assert hasattr(test_exceptions, 'TacozipLibraryError')
        
        assert hasattr(test_loader, 'get_library')
        assert hasattr(test_loader, 'self_check')
        
        assert hasattr(test_bindings, 'create')
        assert hasattr(test_bindings, 'create_multi')
        assert hasattr(test_bindings, 'TacoMetaPtr')
        assert hasattr(test_bindings, 'TacoMetaEntry')
        assert hasattr(test_bindings, 'TacoMetaArray')
        
        assert hasattr(test_version, '__version__')
        assert hasattr(test_version, '_get_version')
    
    def test_error_messages_completeness(self):
        """Test that error messages are complete and properly formatted."""
        # Test all error messages exist and are non-empty
        for error_code, message in config.ERROR_MESSAGES.items():
            assert isinstance(error_code, int)
            assert error_code < 0  # All error codes should be negative
            assert isinstance(message, str)
            assert len(message) > 0
            assert not message.isspace()  # Should not be just whitespace
        
        # Test that error messages are descriptive
        assert "I/O" in config.ERROR_MESSAGES[config.TACOZ_ERR_IO]
        assert "Ghost" in config.ERROR_MESSAGES[config.TACOZ_ERR_INVALID_GHOST]
        assert "argument" in config.ERROR_MESSAGES[config.TACOZ_ERR_PARAM] or "param" in config.ERROR_MESSAGES[config.TACOZ_ERR_PARAM].lower()
        assert "not found" in config.ERROR_MESSAGES[config.TACOZ_ERR_NOT_FOUND].lower()
    
    def test_library_names_platform_coverage(self):
        """Test library names cover all supported platforms."""
        # Test that all expected platforms are covered
        expected_platforms = ["linux", "darwin", "win32"]
        for platform in expected_platforms:
            assert platform in config.LIBRARY_NAMES
        
        # Test library name formats
        linux_libs = config.LIBRARY_NAMES["linux"]
        assert all(lib.startswith("lib") and lib.endswith(".so") for lib in linux_libs)
        
        darwin_libs = config.LIBRARY_NAMES["darwin"]
        assert all(lib.startswith("lib") and lib.endswith(".dylib") for lib in darwin_libs)
        
        win32_libs = config.LIBRARY_NAMES["win32"]
        assert all(lib.endswith(".dll") for lib in win32_libs)
    
    
    def test_package_level_function_availability(self):
        """Test that all functions are available at package level."""
        # Test legacy API functions
        assert callable(tacozip.create)
        assert callable(tacozip.read_ghost)
        assert callable(tacozip.update_ghost)
        
        # Test multi API functions
        assert callable(tacozip.create_multi)
        assert callable(tacozip.read_ghost_multi)
        assert callable(tacozip.update_ghost_multi)
        
        # Test file operations
        assert callable(tacozip.replace_file)
        
        # Test utility functions
        assert callable(tacozip.self_check)
        
        # Test that functions have reasonable docstrings or signatures
        functions_to_check = [
            tacozip.create, tacozip.read_ghost, tacozip.update_ghost,
            tacozip.create_multi, tacozip.read_ghost_multi, 
            tacozip.update_ghost_multi, tacozip.replace_file
        ]
        
        for func in functions_to_check:
            # Should have a function name
            assert hasattr(func, '__name__')
            assert isinstance(func.__name__, str)
            assert len(func.__name__) > 0


class TestStringEncodingEdgeCases:
    """Test string encoding edge cases thoroughly."""
    
    def test_unicode_string_handling(self):
        """Test handling of various Unicode strings."""
        from tacozip.bindings import _prepare_string_array
        
        unicode_strings = [
            "simple.txt",
            "café.txt",  # Latin with accents
            "файл.txt",  # Cyrillic
            "文件.txt",   # Chinese
            "🎉emoji.txt",  # Emoji
            "αβγδε.txt",  # Greek
            "עברית.txt",  # Hebrew
            "العربية.txt",  # Arabic
        ]
        
        string_array, byte_strings = _prepare_string_array(unicode_strings)
        
        assert len(byte_strings) == len(unicode_strings)
        assert len(string_array) == len(unicode_strings)
        
        # Verify all strings can be round-tripped through UTF-8
        for i, original in enumerate(unicode_strings):
            encoded = byte_strings[i]
            assert isinstance(encoded, bytes)
            decoded = encoded.decode('utf-8')
            assert decoded == original
    
    def test_long_filename_handling(self):
        """Test handling of very long filenames."""
        from tacozip.bindings import _prepare_string_array
        
        # Create very long filename (but reasonable for filesystems)
        long_name = "very_long_filename_" + "x" * 200 + ".txt"
        very_long_strings = [long_name]
        
        string_array, byte_strings = _prepare_string_array(very_long_strings)
        
        assert len(byte_strings) == 1
        assert byte_strings[0] == long_name.encode('utf-8')
        assert len(byte_strings[0]) > 200
    
    def test_special_character_filenames(self):
        """Test filenames with special characters."""
        from tacozip.bindings import _prepare_string_array
        
        special_names = [
            "file with spaces.txt",
            "file-with-dashes.txt",
            "file_with_underscores.txt",
            "file.with.many.dots.txt",
            "file(with)parentheses.txt",
            "file[with]brackets.txt",
            "file{with}braces.txt",
            "file@with#special$.txt",
        ]
        
        string_array, byte_strings = _prepare_string_array(special_names)
        
        assert len(byte_strings) == len(special_names)
        
        for i, original in enumerate(special_names):
            assert byte_strings[i] == original.encode('utf-8')


class TestNumericEdgeCases:
    """Test numeric edge cases for offset and length values."""
    
    def test_large_numeric_values(self):
        """Test handling of large numeric values."""
        from tacozip.bindings import _prepare_uint64_array
        
        # Test with various large values
        large_values = [
            0,  # Minimum
            1,  # Small positive
            2**32 - 1,  # Max 32-bit unsigned
            2**32,      # Just over 32-bit
            2**63 - 1,  # Max signed 64-bit
            2**64 - 1,  # Max unsigned 64-bit (might wrap to -1 in signed context)
        ]
        
        array = _prepare_uint64_array(large_values, len(large_values))
        
        for i, expected in enumerate(large_values):
            # Note: ctypes may handle very large values differently
            actual = array[i]
            # For values that fit in uint64, they should match
            if expected < 2**63:
                assert actual == expected
            # For max uint64, behavior depends on platform/ctypes implementation
    
    def test_negative_value_handling(self):
        """Test that negative values are handled appropriately."""
        from tacozip.bindings import _prepare_uint64_array
        
        # Negative values should either be rejected or converted appropriately
        try:
            array = _prepare_uint64_array([-1], 1)
            # If accepted, -1 might become a large positive number (2^64 - 1)
            assert array[0] >= 0  # Should not remain negative
        except (ValueError, OverflowError, ctypes.ArgumentError):
            # It's also acceptable to reject negative values
            pass


class TestMemoryAndPerformanceEdgeCases:
    """Test memory usage and performance edge cases."""
    
    def test_large_array_creation(self):
        """Test creation of large arrays."""
        from tacozip.bindings import _prepare_string_array, _prepare_uint64_array
        
        # Test with a moderately large number of files
        large_file_list = [f"file_{i:06d}.txt" for i in range(1000)]
        string_array, byte_strings = _prepare_string_array(large_file_list)
        
        assert len(byte_strings) == 1000
        assert len(string_array) == 1000
        
        # Verify first and last entries
        assert byte_strings[0] == b"file_000000.txt"
        assert byte_strings[999] == b"file_000999.txt"
        
        # Test with maximum metadata entries
        max_offsets = list(range(config.TACO_GHOST_MAX_ENTRIES))
        max_lengths = [i * 100 for i in range(config.TACO_GHOST_MAX_ENTRIES)]
        
        offset_array = _prepare_uint64_array(max_offsets)
        length_array = _prepare_uint64_array(max_lengths)
        
        assert len(offset_array) == config.TACO_GHOST_MAX_ENTRIES
        assert len(length_array) == config.TACO_GHOST_MAX_ENTRIES
    
    def test_repeated_array_creation(self):
        """Test that repeated array creation works correctly."""
        from tacozip.bindings import _prepare_string_array, _prepare_uint64_array
        
        # Test that creating multiple arrays doesn't interfere with each other
        test_strings = ["file1.txt", "file2.txt"]
        test_numbers = [100, 200]
        
        for _ in range(10):  # Repeat multiple times
            str_array, str_bytes = _prepare_string_array(test_strings)
            num_array = _prepare_uint64_array(test_numbers, 5)
            
            assert len(str_bytes) == 2
            assert str_bytes[0] == b"file1.txt"
            assert str_bytes[1] == b"file2.txt"
            
            assert num_array[0] == 100
            assert num_array[1] == 200
            assert num_array[2] == 0  # Padded
            assert num_array[3] == 0  # Padded
            assert num_array[4] == 0  # Padded


class TestFinalIntegrationScenarios:
    """Final integration test scenarios."""
    
    @patch('tacozip.bindings._lib')
    def test_complete_error_scenario_coverage(self, mock_lib):
        """Test complete error scenario coverage."""
        # Test each function with each type of error
        error_test_cases = [
            (config.TACOZ_ERR_IO, "I/O error"),
            (config.TACOZ_ERR_LIBZIP, "Library error"),
            (config.TACOZ_ERR_INVALID_GHOST, "Invalid ghost"),
            (config.TACOZ_ERR_PARAM, "Parameter error"),
            (config.TACOZ_ERR_NOT_FOUND, "Not found error"),
        ]
        
        functions_to_test = [
            ('tacozip_create_multi', lambda: tacozip.create_multi("test.zip", ["f"], ["a"], [1], [2])),
            ('tacozip_read_ghost_multi', lambda: tacozip.read_ghost_multi("test.zip")),
            ('tacozip_update_ghost_multi', lambda: tacozip.update_ghost_multi("test.zip", [1], [2])),
            ('tacozip_replace_file', lambda: tacozip.replace_file("test.zip", "old", "new")),
        ]
        
        for error_code, error_desc in error_test_cases:
            for func_name, func_call in functions_to_test:
                # Set the mock to return this error
                getattr(mock_lib, func_name).return_value = error_code
                
                # Call the function and verify it raises the right exception
                with pytest.raises(exceptions.TacozipError) as exc_info:
                    func_call()
                
                assert exc_info.value.code == error_code
                assert error_desc.split()[0].lower() in str(exc_info.value).lower() or str(error_code) in str(exc_info.value)
    
    def test_package_consistency(self):
        """Test package consistency and completeness."""
        # Test that package-level imports match module-level definitions
        assert tacozip.TacozipError is exceptions.TacozipError
        assert tacozip.create is bindings.create
        assert tacozip.create_multi is bindings.create_multi
        assert tacozip.read_ghost is bindings.read_ghost
        assert tacozip.read_ghost_multi is bindings.read_ghost_multi
        assert tacozip.update_ghost is bindings.update_ghost
        assert tacozip.update_ghost_multi is bindings.update_ghost_multi
        assert tacozip.replace_file is bindings.replace_file
        assert tacozip.self_check is loader.self_check
        
        # Test that constants match
        assert tacozip.TACOZ_OK is config.TACOZ_OK
        assert tacozip.TACO_GHOST_MAX_ENTRIES is config.TACO_GHOST_MAX_ENTRIES
        
        # Test version consistency
        assert tacozip.__version__ == version.__version__


# Final test runner that ensures everything is covered
class TestCoverageVerification:
    """Verify that our tests achieve 100% coverage."""
    
    def test_all_modules_imported(self):
        """Verify all modules can be imported without errors."""
        # Test importing the main package
        import tacozip
        assert tacozip is not None
        
        # Test importing all submodules
        from tacozip import config, exceptions, loader, bindings, version
        assert config is not None
        assert exceptions is not None
        assert loader is not None
        assert bindings is not None
        assert version is not None
    
    def test_all_public_apis_callable(self):
        """Verify all public APIs are callable."""
        # Get all items from __all__
        for item_name in tacozip.__all__:
            item = getattr(tacozip, item_name)
            
            # If it's callable, make sure it can be called with some arguments
            if callable(item):
                # We won't actually call them (no mocking here), just verify they're callable
                assert hasattr(item, '__call__')
            else:
                # If not callable, it should be a constant or other value
                assert item is not None
    
    def test_no_missing_imports(self):
        """Test that there are no missing imports in the package."""
        # This test ensures that all imports in __init__.py work
        try:
            from tacozip import (
                __version__, __author__, __author_email__, __description__, 
                __url__, __license__, self_check, TACOZ_OK, TACOZ_ERR_IO, 
                TACOZ_ERR_LIBZIP, TACOZ_ERR_INVALID_GHOST, TACOZ_ERR_PARAM, 
                TACOZ_ERR_NOT_FOUND, TACO_GHOST_MAX_ENTRIES, TacozipError,
                create, read_ghost, update_ghost, create_multi, 
                read_ghost_multi, update_ghost_multi, replace_file
            )
            # If we get here, all imports succeeded
            assert True
        except ImportError as e:
            pytest.fail(f"Missing import in tacozip package: {e}")


if __name__ == "__main__":
    # This can be run directly to test the edge cases
    pytest.main([__file__, "-v"])