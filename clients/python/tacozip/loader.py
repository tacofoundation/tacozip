"""Native library loader for tacozip."""
import sys
import ctypes
from pathlib import Path
from .exceptions import TacozipLibraryError


def _load_shared():
    """Load the tacozip shared library."""
    plat = sys.platform
    names = []
    if plat.startswith("linux"):
        names = ["libtacozip.so"]
    elif plat == "darwin":
        names = ["libtacozip.dylib"]
    elif plat == "win32":
        names = ["tacozip.dll", "libtacozip.dll"]
    else:
        names = ["libtacozip.so"]

    here = Path(__file__).parent
    
    # First try to find library in the package directory
    for n in names:
        p = here / n
        if p.exists():
            try:
                return ctypes.CDLL(str(p))
            except OSError as e:
                print(f"Failed to load {p}: {e}")
                continue

    # Debug: list all files in the package directory
    print(f"Available files in {here}:")
    if here.exists():
        for item in here.iterdir():
            print(f"  - {item.name}")
    else:
        print(f"  Package directory {here} does not exist!")

    raise TacozipLibraryError(
        -1, 
        f"Native library not found. Searched for: {names}. "
        f"Available files in package: {[f.name for f in here.iterdir() if here.exists()]}"
    )

# Load the shared library
_lib = _load_shared()

def get_library():
    """Get the loaded native library."""
    return _lib

def self_check():
    """Perform self-check of the native library."""
    lib = get_library()
    
    required_functions = [
        'tacozip_get_version',
        'tacozip_create',
        'tacozip_update_ghost',
        'tacozip_append_files',
        'tacozip_replace_file',
        'tacozip_read_ghost',
    ]
    
    missing_functions = []
    for func_name in required_functions:
        if not hasattr(lib, func_name):
            missing_functions.append(func_name)
    
    if missing_functions:
        raise TacozipLibraryError(
            -1, f"Missing functions in native library: {missing_functions}"
        )