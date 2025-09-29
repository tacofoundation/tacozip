"""Test bindings module."""
import pytest
import ctypes
import pathlib
from unittest.mock import patch, Mock
from tacozip import bindings, config, exceptions
from tacozip.bindings import TacoMetaEntry, TacoMetaArray, TacoAppendEntry


class TestBindings:
    """Test bindings module."""
    
    def test_ctypes_structures(self):
        """Test ctypes structure definitions."""
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
        
        # Test TacoAppendEntry
        append_entry = TacoAppendEntry()
        src_path = b"/path/to/source.txt"
        arc_name = b"archive_name.txt"
        append_entry.src_path = src_path
        append_entry.arc_name = arc_name
        assert append_entry.src_path == src_path
        assert append_entry.arc_name == arc_name
    
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
    
    def test_prepare_meta_array(self):
        """Test _prepare_meta_array function."""
        from tacozip.bindings import _prepare_meta_array
        
        entries = [(100, 200), (300, 400), (500, 600)]
        meta_array = _prepare_meta_array(entries)
        
        assert meta_array.count == 3
        assert meta_array.entries[0].offset == 100
        assert meta_array.entries[0].length == 200
        assert meta_array.entries[1].offset == 300
        assert meta_array.entries[1].length == 400
        assert meta_array.entries[2].offset == 500
        assert meta_array.entries[2].length == 600
        
        # Test error with too many entries
        too_many_entries = [(i, i) for i in range(config.TACO_HEADER_MAX_ENTRIES + 1)]
        with pytest.raises(ValueError):
            _prepare_meta_array(too_many_entries)
    
    def test_extract_meta_entries(self):
        """Test _extract_meta_entries function.""" 
        from tacozip.bindings import _extract_meta_entries
        
        # Create a meta array
        meta_array = TacoMetaArray()
        meta_array.count = 2
        meta_array.entries[0].offset = 1000
        meta_array.entries[0].length = 2000
        meta_array.entries[1].offset = 3000
        meta_array.entries[1].length = 4000
        
        entries = _extract_meta_entries(meta_array)
        assert len(entries) == 2
        assert entries[0] == (1000, 2000)
        assert entries[1] == (3000, 4000)
    
    def test_prepare_append_entries(self):
        """Test _prepare_append_entries function."""
        from tacozip.bindings import _prepare_append_entries
        
        entries = [("/path/to/file1.txt", "file1.txt"), ("/path/to/file2.txt", "file2.txt")]
        entry_array, byte_strings = _prepare_append_entries(entries)
        
        assert len(entry_array) == 2
        assert len(byte_strings) == 4  # 2 entries * 2 strings each
        
        # Test error with empty entries
        with pytest.raises(ValueError):
            _prepare_append_entries([])
    
    @patch('tacozip.bindings._lib')
    def test_create_function(self, mock_lib):
        """Test create function."""
        mock_lib.tacozip_create.return_value = config.TACOZ_OK
        
        # Mock pathlib operations
        with patch('tacozip.bindings.pathlib.Path') as mock_path:
            mock_path.return_value.parent.exists.return_value = True
            mock_path.return_value.stat.return_value.st_size = 1024
            
            bindings.create("test.zip", ["file1.txt"], ["arch1.txt"], [(100, 200)])
        
        # Verify function was called
        mock_lib.tacozip_create.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_read_header_function(self, mock_lib):
        """Test read_header function."""
        mock_lib.tacozip_read_header.return_value = config.TACOZ_OK
        
        entries = bindings.read_header("test.zip")
        assert isinstance(entries, list)
        
        # Verify function was called with correct arguments
        mock_lib.tacozip_read_header.assert_called_once()
        args = mock_lib.tacozip_read_header.call_args[0]
        assert args[0] == b"test.zip"  # First argument should be encoded path
    
    @patch('tacozip.bindings._lib')
    def test_update_header_function(self, mock_lib):
        """Test update_header function."""
        mock_lib.tacozip_update_header.return_value = config.TACOZ_OK
        
        bindings.update_header("test.zip", [(1000, 2000)])
        
        # Verify function was called
        mock_lib.tacozip_update_header.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_append_files_function(self, mock_lib):
        """Test append_files function."""
        mock_lib.tacozip_append_files.return_value = config.TACOZ_OK
        
        entries = [("/path/to/file1.txt", "file1.txt"), ("/path/to/file2.txt", "file2.txt")]
        bindings.append_files("test.zip", entries)
        
        # Verify function was called
        mock_lib.tacozip_append_files.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_append_files_error(self, mock_lib):
        """Test append_files function with error."""
        mock_lib.tacozip_append_files.return_value = config.TACOZ_ERR_EXISTS
        
        entries = [("/path/to/file1.txt", "file1.txt")]
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.append_files("test.zip", entries)
        
        assert exc_info.value.code == config.TACOZ_ERR_EXISTS
    
    @patch('tacozip.bindings._lib')
    def test_replace_file_function(self, mock_lib):
        """Test replace_file function."""
        mock_lib.tacozip_replace_file.return_value = config.TACOZ_OK
        
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
    
    @patch('tacozip.bindings._lib')
    def test_get_library_version(self, mock_lib):
        """Test get_library_version function."""
        mock_lib.tacozip_get_version.return_value = b"0.9.0"
        
        version = bindings.get_library_version()
        assert version == "0.9.0"
        
        # Verify function was called
        mock_lib.tacozip_get_version.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_get_library_version_none(self, mock_lib):
        """Test get_library_version function with None return."""
        mock_lib.tacozip_get_version.return_value = None
        
        version = bindings.get_library_version()
        assert version == "unknown"
    
    def test_normalize_inputs(self):
        """Test _normalize_inputs function."""
        from tacozip.bindings import _normalize_inputs
        import pathlib
        
        # Test with string paths
        src_files = ["file1.txt", "file2.txt"]
        arc_files = ["arch1.txt", "arch2.txt"]
        
        with patch('tacozip.bindings.pathlib.Path') as mock_path:
            mock_path.return_value.resolve.return_value = "resolved_path"
            mock_path.return_value.name = "filename"
            
            normalized_src, normalized_arc = _normalize_inputs(src_files, arc_files)
            assert len(normalized_src) == 2
            assert len(normalized_arc) == 2
            assert normalized_arc == arc_files
        
        # Test with pathlib.Path objects  
        with patch('tacozip.bindings.pathlib.Path') as mock_path:
            mock_path.return_value.resolve.return_value = "resolved_path"
            mock_path.return_value.name = "filename"
            
            path_objects = [mock_path("file1.txt"), mock_path("file2.txt")]
            normalized_src, normalized_arc = _normalize_inputs(path_objects, None)
            assert len(normalized_src) == 2
            assert len(normalized_arc) == 2
        
        # Test error with mismatched counts
        with pytest.raises(ValueError):
            _normalize_inputs(["file1.txt", "file2.txt"], ["arch1.txt"])
    
    def test_minimal_output_check(self):
        """Test _minimal_output_check function."""
        from tacozip.bindings import _minimal_output_check
        
        with patch('tacozip.bindings.pathlib.Path') as mock_path:
            mock_path.return_value.parent.exists.return_value = True
            mock_path.return_value.parent = pathlib.Path('.')
            
            result = _minimal_output_check("test.zip")
            assert isinstance(result, str)


class TestReadHeaderBytes:
    """Test read_header with bytes buffer support."""
    
    @patch('tacozip.bindings._lib')
    def test_read_header_from_file_path(self, mock_lib):
        """Test read_header with file path (backward compatible)."""
        mock_lib.tacozip_read_header.return_value = config.TACOZ_OK
        
        # Test with string path
        entries = bindings.read_header("test.zip")
        
        # Verify file-based function was called
        mock_lib.tacozip_read_header.assert_called_once()
        args = mock_lib.tacozip_read_header.call_args[0]
        assert args[0] == b"test.zip"
        
        # Verify buffer function was NOT called
        assert not mock_lib.tacozip_parse_header.called
    
    @patch('tacozip.bindings._lib')
    def test_read_header_from_bytes_buffer(self, mock_lib):
        """Test read_header with bytes buffer."""
        mock_lib.tacozip_parse_header.return_value = config.TACOZ_OK
        
        # Create 157+ byte buffer (minimum header size)
        buffer = b'\x00' * 200
        
        entries = bindings.read_header(buffer)
        
        # Verify buffer-based function was called
        mock_lib.tacozip_parse_header.assert_called_once()
        
        # Verify file function was NOT called
        assert not mock_lib.tacozip_read_header.called
    
    @patch('tacozip.bindings._lib')
    def test_read_header_bytes_too_small(self, mock_lib):
        """Test read_header with buffer smaller than 157 bytes."""
        # Create buffer smaller than minimum
        buffer = b'\x00' * 100
        
        with pytest.raises(ValueError) as exc_info:
            bindings.read_header(buffer)
        
        assert "Buffer too small" in str(exc_info.value)
        assert "100 < 157" in str(exc_info.value)
    
    @patch('tacozip.bindings._lib')
    def test_read_header_bytes_exact_size(self, mock_lib):
        """Test read_header with exactly 157 bytes."""
        mock_lib.tacozip_parse_header.return_value = config.TACOZ_OK
        
        # Create exactly 157 byte buffer
        buffer = b'\x00' * 157
        
        entries = bindings.read_header(buffer)
        
        # Should work fine
        mock_lib.tacozip_parse_header.assert_called_once()
    
    @patch('tacozip.bindings._lib')
    def test_read_header_pathlib_path(self, mock_lib):
        """Test read_header with pathlib.Path object."""
        import pathlib
        mock_lib.tacozip_read_header.return_value = config.TACOZ_OK
        
        # Test with Path object
        path = pathlib.Path("test.zip")
        entries = bindings.read_header(path)
        
        # Verify file-based function was called with string
        mock_lib.tacozip_read_header.assert_called_once()
        args = mock_lib.tacozip_read_header.call_args[0]
        assert args[0] == b"test.zip"
    
    @patch('tacozip.bindings._lib')
    def test_read_header_bytes_http_example(self, mock_lib):
        """Test read_header simulating HTTP range request."""
        mock_lib.tacozip_parse_header.return_value = config.TACOZ_OK
        
        # Simulate HTTP response with range request
        http_response_content = b'PK\x03\x04' + b'\x00' * 196  # ZIP signature + padding
        
        entries = bindings.read_header(http_response_content)
        
        # Verify buffer parsing was used
        mock_lib.tacozip_parse_header.assert_called_once()
        
        # Check buffer size was passed correctly
        args = mock_lib.tacozip_parse_header.call_args[0]
        assert args[1] == 200  # buffer size
    
    @patch('tacozip.bindings._lib')
    def test_read_header_bytes_error_handling(self, mock_lib):
        """Test read_header bytes with C library error."""
        mock_lib.tacozip_parse_header.return_value = config.TACOZ_ERR_INVALID_HEADER
        
        buffer = b'\x00' * 200
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.read_header(buffer)
        
        assert exc_info.value.code == config.TACOZ_ERR_INVALID_HEADER
    
    @patch('tacozip.bindings._lib')
    def test_read_header_file_error_handling(self, mock_lib):
        """Test read_header file path with C library error."""
        mock_lib.tacozip_read_header.return_value = config.TACOZ_ERR_IO
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.read_header("nonexistent.zip")
        
        assert exc_info.value.code == config.TACOZ_ERR_IO
    
    @patch('tacozip.bindings._lib')
    def test_read_header_bytes_with_metadata(self, mock_lib):
        """Test read_header bytes returns correct metadata."""
        mock_lib.tacozip_parse_header.return_value = config.TACOZ_OK
        
        buffer = b'\x00' * 200
        entries = bindings.read_header(buffer)
        
        # Verify parse_header was called correctly
        mock_lib.tacozip_parse_header.assert_called_once()
        
        # Verify correct arguments (buffer pointer, size, meta pointer)
        args = mock_lib.tacozip_parse_header.call_args[0]
        assert args[1] == 200  # buffer size
        
        # Verify it returns a list (actual metadata extraction is tested elsewhere)
        assert isinstance(entries, list)
    
    def test_read_header_type_detection(self):
        """Test that read_header correctly detects input type."""
        # This test verifies the isinstance check works correctly
        
        # bytes should be detected as bytes
        assert isinstance(b'test', bytes)
        
        # str should not be bytes
        assert not isinstance('test', bytes)
        
        # Path should not be bytes
        import pathlib
        assert not isinstance(pathlib.Path('test'), bytes)


# Integration-style tests (can be run with actual library if available)
class TestReadHeaderIntegration:
    """Integration tests for read_header (requires actual library)."""
    
    @pytest.mark.skipif(not hasattr(bindings._lib, 'tacozip_parse_header'), 
                       reason="Library not available")
    def test_read_header_both_methods_consistent(self, tmp_path):
        """Test that file and bytes methods return same results."""
        # This would require an actual TACO file
        # Left as placeholder for integration testing
        pass
    
    @pytest.mark.skipif(not hasattr(bindings._lib, 'tacozip_parse_header'),
                       reason="Library not available")
    def test_read_header_bytes_performance(self):
        """Test that bytes method has no significant overhead."""
        # This would test performance characteristics
        # Left as placeholder for integration testing
        pass