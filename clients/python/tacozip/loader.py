import sys
import ctypes
from pathlib import Path
from .exceptions import TacozipLibraryError


def _load_shared():
    """Load the tacozip shared library."""
    plat = sys.platform
    
    if plat.startswith("linux"):
        names = ["libtacozip.so"]
    elif plat == "darwin":
        names = ["libtacozip.dylib"]
    elif plat == "win32":
        names = ["tacozip.dll", "libtacozip.dll"]
    else:
        names = ["libtacozip.so"]

    here = Path(__file__).parent

    for n in names:
        p = here / n
        if p.exists():
            try:
                return ctypes.CDLL(str(p))
            except OSError:
                continue

    available = [f.name for f in here.iterdir()] if here.exists() else []
    raise TacozipLibraryError(
        -1,
        f"Native library not found. Searched: {names}. Available: {available}",
    )


_lib = _load_shared()


def get_library():
    """Get the loaded native library."""
    return _lib


def self_check():
    """Perform self-check of the native library."""
    lib = get_library()

    required_functions = [
        "tacozip_get_version",
        "tacozip_create",
        "tacozip_read_header",
        "tacozip_update_header",
        "tacozip_parse_header",
        "tacozip_serialize_header",
    ]

    missing_functions = [f for f in required_functions if not hasattr(lib, f)]

    if missing_functions:
        raise TacozipLibraryError(
            -1, f"Missing functions in native library: {missing_functions}"
        )