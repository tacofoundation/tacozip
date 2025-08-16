"""Test bindings module."""
import pytest
import ctypes
from unittest.mock import patch, Mock
from tacozip import bindings, config, exceptions
from tacozip.bindings import TacoMetaPtr, TacoMetaEntry, TacoMetaArray


class TestBindings:
    """Test bindings module."""
    
    def test_ctypes_structures(self):
        """Test ctypes structure definitions."""
        # Test TacoMetaPtr
        meta_ptr = TacoMetaPtr()
        meta_ptr.offset = 12345
        meta_ptr.length = 67890
        assert meta_ptr.offset == 12345
        assert meta_ptr.length == 67890
        
        # Test TacoMetaEntry
        meta_entry = TacoMetaEntry()
        meta_entry.offset = 11111
        meta_entry.length = 22222
        assert meta_entry.offset == 11111
        assert meta_entry.length == 22222
        
        # Test TacoMetaArray
        meta_array = TacoMetaArray()
        meta_array.count = 3
        assert meta_array.count == 3
        
        # Test array entries
        for i in range(3):
            meta_array.entries[i].offset = i * 1000
            meta_array.entries[i].length = i * 500
            assert meta_array.entries[i].offset == i * 1000
            assert meta_array.entries[i].length == i * 500
    
    def test_check_result_success(self):
        """Test _check_result with success code."""
        from tacozip.bindings import _check_result
        # Should not raise
        _check_result(config.TACOZ_OK)
    
    def test_check_result_error(self):
        """Test _check_result with error codes."""
        from tacozip.bindings import _check_result
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            _check_result(config.TACOZ_ERR_IO)
        assert exc_info.value.code == config.TACOZ_ERR_IO
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            _check_result(config.TACOZ_ERR_PARAM)
        assert exc_info.value.code == config.TACOZ_ERR_PARAM
    
    def test_prepare_string_array(self):
        """Test _prepare_string_array function."""
        from tacozip.bindings import _prepare_string_array
        
        strings = ["file1.txt", "file2.txt", "file3.txt"]
        string_array, byte_strings = _prepare_string_array(strings)
        
        assert len(byte_strings) == 3
        assert len(string_array) == 3
        
        for i, original in enumerate(strings):
            assert byte_strings[i] == original.encode('utf-8')
        
        # Test empty array
        empty_array, empty_bytes = _prepare_string_array([])
        assert len(empty_array) == 0
        assert len(empty_bytes) == 0
    
    def test_prepare_uint64_array(self):
        """Test _prepare_uint64_array function."""
        from tacozip.bindings import _prepare_uint64_array
        
        values = [100, 200, 300]
        array = _prepare_uint64_array(values, 5)
        
        assert len(array) == 5
        assert array[0] == 100
        assert array[1] == 200
        assert array[2] == 300
        assert array[3] == 0  # Padded
        assert array[4] == 0  # Padded
        
        # Test error with too many values
        with pytest.raises(ValueError):
            _prepare_uint64_array([1, 2, 3, 4, 5, 6], 3)
    
    @patch('tacozip.bindings._lib')
    def test_create_function(self, mock_lib):
        """Test create function."""
        mock_lib.tacozip_create.return_value = config.TACOZ_OK
        
        result = bindings.create("test.zip", ["file1.txt"], ["arch1.txt"], 100, 200)
        assert result == config.TACOZ_OK
        
        # Verify function was called
        mock_lib.tacozip_create.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_read_ghost_function(self, mock_lib):
        """Test read_ghost function."""
        # Simple mock without trying to modify ctypes structures
        mock_lib.tacozip_read_ghost.return_value = config.TACOZ_OK
        
        rc, offset, length = bindings.read_ghost("test.zip")
        assert rc == config.TACOZ_OK
        
        # Verify function was called with correct arguments
        mock_lib.tacozip_read_ghost.assert_called_once()
        args = mock_lib.tacozip_read_ghost.call_args[0]
        assert args[0] == b"test.zip"  # First argument should be encoded path
    
    @patch('tacozip.bindings._lib')
    def test_update_ghost_function(self, mock_lib):
        """Test update_ghost function."""
        mock_lib.tacozip_update_ghost.return_value = config.TACOZ_OK
        
        result = bindings.update_ghost("test.zip", 1000, 2000)
        assert result == config.TACOZ_OK
        
        # Verify function was called
        mock_lib.tacozip_update_ghost.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_create_multi_function(self, mock_lib):
        """Test create_multi function."""
        mock_lib.tacozip_create_multi.return_value = config.TACOZ_OK
        
        # Should not raise
        bindings.create_multi("test.zip", ["file1.txt"], ["arch1.txt"], [100], [200])
        
        # Verify function was called
        mock_lib.tacozip_create_multi.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_create_multi_error(self, mock_lib):
        """Test create_multi function with error."""
        mock_lib.tacozip_create_multi.return_value = config.TACOZ_ERR_IO
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.create_multi("test.zip", ["file1.txt"], ["arch1.txt"], [100], [200])
        
        assert exc_info.value.code == config.TACOZ_ERR_IO
    
    @patch('tacozip.bindings._lib')
    def test_read_ghost_multi_function(self, mock_lib):
        """Test read_ghost_multi function."""
        # Simple mock without trying to modify ctypes structures
        mock_lib.tacozip_read_ghost_multi.return_value = config.TACOZ_OK
        
        count, entries = bindings.read_ghost_multi("test.zip")
        assert count == 0  # Default value when not modified
        assert len(entries) == config.TACO_GHOST_MAX_ENTRIES
        
        # Verify function was called
        mock_lib.tacozip_read_ghost_multi.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_update_ghost_multi_function(self, mock_lib):
        """Test update_ghost_multi function."""
        mock_lib.tacozip_update_ghost_multi.return_value = config.TACOZ_OK
        
        # Should not raise
        bindings.update_ghost_multi("test.zip", [100, 200], [300, 400])
        
        # Verify function was called
        mock_lib.tacozip_update_ghost_multi.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_replace_file_function(self, mock_lib):
        """Test replace_file function."""
        mock_lib.tacozip_replace_file.return_value = config.TACOZ_OK
        
        # Should not raise
        bindings.replace_file("test.zip", "old.txt", "new.txt")
        
        # Verify function was called
        mock_lib.tacozip_replace_file.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_replace_file_error(self, mock_lib):
        """Test replace_file function with error."""
        mock_lib.tacozip_replace_file.return_value = config.TACOZ_ERR_NOT_FOUND
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.replace_file("test.zip", "old.txt", "new.txt")
        
        assert exc_info.value.code == config.TACOZ_ERR_NOT_FOUND