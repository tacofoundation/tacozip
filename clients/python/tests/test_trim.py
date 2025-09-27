"""Test trim_from functionality."""
import pytest
from unittest.mock import patch, Mock
from tacozip import bindings, config, exceptions


class TestTrimFrom:
    """Test trim_from function."""
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_metadata_success(self, mock_lib):
        """Test trim_from with METADATA/ target - success case."""
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_OK
        
        # Should not raise
        bindings.trim_from("test.taco", "METADATA/")
        
        # Verify function was called with correct arguments
        mock_lib.tacozip_trim_from.assert_called_once()
        args = mock_lib.tacozip_trim_from.call_args[0]
        assert args[0] == b"test.taco"  # zip_path encoded
        assert args[1] == b"METADATA/"  # target encoded
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_collection_success(self, mock_lib):
        """Test trim_from with COLLECTION.json target - success case."""
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_OK
        
        # Should not raise
        bindings.trim_from("archive.taco", "COLLECTION.json")
        
        # Verify function was called with correct arguments
        mock_lib.tacozip_trim_from.assert_called_once()
        args = mock_lib.tacozip_trim_from.call_args[0]
        assert args[0] == b"archive.taco"
        assert args[1] == b"COLLECTION.json"
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_not_found_error(self, mock_lib):
        """Test trim_from when target not found in archive."""
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_ERR_NOT_FOUND
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.trim_from("test.taco", "METADATA/")
        
        assert exc_info.value.code == config.TACOZ_ERR_NOT_FOUND
        assert "File not found in archive" in str(exc_info.value)
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_invalid_target_error(self, mock_lib):
        """Test trim_from with invalid target."""
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_ERR_PARAM
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.trim_from("test.taco", "DATA/")  # Invalid target
        
        assert exc_info.value.code == config.TACOZ_ERR_PARAM
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_unsafe_operation_error(self, mock_lib):
        """Test trim_from when operation is unsafe (files exist after trim point)."""
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_ERR_PARAM
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.trim_from("test.taco", "METADATA/")  # Files exist after METADATA/
        
        assert exc_info.value.code == config.TACOZ_ERR_PARAM
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_io_error(self, mock_lib):
        """Test trim_from with I/O error."""
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_ERR_IO
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.trim_from("nonexistent.taco", "METADATA/")
        
        assert exc_info.value.code == config.TACOZ_ERR_IO
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_invalid_header_error(self, mock_lib):
        """Test trim_from with malformed ZIP."""
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_ERR_INVALID_HEADER
        
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.trim_from("corrupted.taco", "COLLECTION.json")
        
        assert exc_info.value.code == config.TACOZ_ERR_INVALID_HEADER
    
    def test_trim_from_encoding_handling(self):
        """Test trim_from properly handles string encoding."""
        with patch('tacozip.bindings._lib') as mock_lib:
            mock_lib.tacozip_trim_from.return_value = config.TACOZ_OK
            
            # Test with unicode characters
            bindings.trim_from("test_ñ.taco", "METADATA/")
            
            args = mock_lib.tacozip_trim_from.call_args[0]
            assert args[0] == "test_ñ.taco".encode('utf-8')
            assert args[1] == b"METADATA/"
    
    def test_trim_from_target_validation(self):
        """Test that trim_from validates target format.""" 
        with patch('tacozip.bindings._lib') as mock_lib:
            mock_lib.tacozip_trim_from.return_value = config.TACOZ_OK
            
            # Valid targets should work
            valid_targets = ["METADATA/", "COLLECTION.json"]
            for target in valid_targets:
                bindings.trim_from("test.taco", target)
                args = mock_lib.tacozip_trim_from.call_args[0]
                assert args[1] == target.encode('utf-8')
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_typical_workflow(self, mock_lib):
        """Test typical workflow: trim -> append -> trim -> append."""
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_OK
        mock_lib.tacozip_append_files.return_value = config.TACOZ_OK
        
        # Step 1: Remove old collection.json
        bindings.trim_from("archive.taco", "COLLECTION.json")
        
        # Step 2: Append new metadata (would be done separately)
        # This just verifies the trim call was made correctly
        mock_lib.tacozip_trim_from.assert_called_with(
            b"archive.taco", 
            b"COLLECTION.json"
        )
        
        # Reset mock for next call
        mock_lib.reset_mock()
        
        # Step 3: Remove metadata folder
        bindings.trim_from("archive.taco", "METADATA/")
        
        mock_lib.tacozip_trim_from.assert_called_with(
            b"archive.taco", 
            b"METADATA/"
        )


class TestTrimFromEdgeCases:
    """Test edge cases for trim_from function."""
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_empty_strings(self, mock_lib):
        """Test trim_from with empty strings."""
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_ERR_PARAM
        
        with pytest.raises(exceptions.TacozipError):
            bindings.trim_from("", "METADATA/")
        
        with pytest.raises(exceptions.TacozipError):
            bindings.trim_from("test.taco", "")
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_none_parameters(self, mock_lib):
        """Test trim_from with None parameters."""
        # Configure mock to return proper error code
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_ERR_PARAM
        
        # str(None) becomes "None", so it actually calls the C function
        # but should still raise TacozipError with PARAM error
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.trim_from(None, "METADATA/")
        
        assert exc_info.value.code == config.TACOZ_ERR_PARAM
        
        # Test with None target as well
        with pytest.raises(exceptions.TacozipError) as exc_info:
            bindings.trim_from("test.taco", None)
        
        assert exc_info.value.code == config.TACOZ_ERR_PARAM
    
    @patch('tacozip.bindings._lib')
    def test_trim_from_path_objects(self, mock_lib):
        """Test trim_from accepts path-like objects."""
        from pathlib import Path
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_OK
        
        # Should work with pathlib.Path
        path_obj = Path("test.taco")
        bindings.trim_from(path_obj, "METADATA/")
        
        args = mock_lib.tacozip_trim_from.call_args[0]
        assert args[0] == b"test.taco"
        assert args[1] == b"METADATA/"


class TestTrimFromIntegration:
    """Integration-style tests for trim_from (still mocked but more realistic)."""
    
    @patch('tacozip.bindings._lib')
    def test_rebuild_workflow_simulation(self, mock_lib):
        """Simulate a typical rebuild workflow."""
        # All operations succeed
        mock_lib.tacozip_trim_from.return_value = config.TACOZ_OK
        mock_lib.tacozip_append_files.return_value = config.TACOZ_OK
        
        archive_path = "test_rebuild.taco"
        
        # Step 1: Remove old metadata
        bindings.trim_from(archive_path, "METADATA/")
        
        # Step 2: Would append new metadata files (simulated)
        new_metadata = [
            ("/path/to/new_file1.parquet", "METADATA/file1.parquet"),
            ("/path/to/new_file2.parquet", "METADATA/file2.parquet")
        ]
        bindings.append_files(archive_path, new_metadata)
        
        # Step 3: Remove old collection.json 
        bindings.trim_from(archive_path, "COLLECTION.json")
        
        # Step 4: Would append new collection.json (simulated)
        new_collection = [("/path/to/new_collection.json", "COLLECTION.json")]
        bindings.append_files(archive_path, new_collection)
        
        # Verify all calls were made correctly
        assert mock_lib.tacozip_trim_from.call_count == 2
        assert mock_lib.tacozip_append_files.call_count == 2
        
        # Verify trim calls
        trim_calls = mock_lib.tacozip_trim_from.call_args_list
        assert trim_calls[0][0] == (b"test_rebuild.taco", b"METADATA/")
        assert trim_calls[1][0] == (b"test_rebuild.taco", b"COLLECTION.json")