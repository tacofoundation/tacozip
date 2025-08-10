import os
import sys
from pathlib import Path

def _lib_name():
    """Return the expected native library filename for this platform."""
    if sys.platform.startswith("win"):
        return "tacozip.dll"
    elif sys.platform == "darwin":
        return "libtacozip.dylib"
    else:
        return "libtacozip.so"

def _self_check():
    """Verify that the native library can be found and loaded."""
    here = Path(__file__).parent
    lib_name = _lib_name()
    lib_path = here / lib_name
    
    if not lib_path.exists():
        raise ImportError(f"Native library {lib_name} not found at: {lib_path}")
    
    # Try to load the library to ensure it's valid
    try:
        from . import _load_shared
        _load_shared()
    except Exception as e:
        raise ImportError(f"Failed to load native library {lib_name}: {e}")

# Allow this to be called directly
if __name__ == "__main__":
    _self_check()
    print("tacozip self-check passed")