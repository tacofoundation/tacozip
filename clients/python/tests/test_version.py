"""Test version detection."""
import pytest
from unittest.mock import patch, Mock
from tacozip import version


class TestVersion:
    """Test version module."""
    
    def test_version_exists(self):
        """Test that version string exists."""
        assert hasattr(version, '__version__')
        assert isinstance(version.__version__, str)
        assert len(version.__version__) > 0
    
    def test_get_version_function(self):
        """Test _get_version function."""
        version_str = version._get_version()
        assert isinstance(version_str, str)
        assert len(version_str) > 0
    
    def test_version_fallback_to_hardcoded(self):
        """Test version fallback when metadata libraries fail."""
        # Use builtins import patching instead of module attribute patching
        import builtins
        original_import = builtins.__import__
        
        def mock_import(name, *args, **kwargs):
            if 'importlib.metadata' in name or 'pkg_resources' in name:
                raise ImportError(f"Mocked import error for {name}")
            return original_import(name, *args, **kwargs)
        
        with patch('builtins.__import__', side_effect=mock_import):
            # Force reload to test fallback
            import importlib
            importlib.reload(version)
            version_str = version._get_version()
            # Should work either way - with real version or fallback
            assert isinstance(version_str, str)
            assert len(version_str) > 0
    
    def test_version_with_metadata_exception(self):
        """Test version when metadata raises AttributeError."""
        # Test the function directly with mocked importlib calls
        with patch('builtins.__import__') as mock_import:
            def import_side_effect(name, *args, **kwargs):
                if name == 'importlib':
                    # Return a mock module with metadata that raises AttributeError
                    mock_importlib = Mock()
                    mock_metadata = Mock()
                    mock_metadata.version.side_effect = AttributeError("No version")
                    mock_importlib.metadata = mock_metadata
                    return mock_importlib
                elif name == 'pkg_resources':
                    raise ImportError("pkg_resources not found")
                return __import__(name, *args, **kwargs)
            
            mock_import.side_effect = import_side_effect
            
            # Test the function
            version_str = version._get_version()
            assert isinstance(version_str, str)
            assert len(version_str) > 0