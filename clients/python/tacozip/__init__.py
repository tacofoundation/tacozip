#!/usr/bin/env python3
"""
tacozip - TACO ZIP: ZIP64 archive with TACO Ghost supporting up to 7 metadata entries

High-performance ZIP64 writer with specialized metadata support for parquet files.
"""

import ctypes
import sys
import os
from pathlib import Path
from typing import List, Tuple, Optional

# Version detection with fallback
try:
    from importlib import metadata
    __version__ = metadata.version("tacozip")
except Exception:
    try:
        import pkg_resources
        __version__ = pkg_resources.get_distribution("tacozip").version
    except Exception:
        __version__ = "0.3.0"  # fallback

# Package metadata
__author__ = "Cesar Aybar"
__author_email__ = "csaybar@gmail.com"
__description__ = "TACO ZIP: ZIP64 archive with TACO Ghost supporting up to 7 metadata entries"
__url__ = "https://github.com/csaybar/tacozip"
__license__ = "MIT"

# Error codes from C library
TACOZ_OK = 0
TACOZ_ERR_IO = -1
TACOZ_ERR_LIBZIP = -2
TACOZ_ERR_INVALID_GHOST = -3
TACOZ_ERR_PARAM = -4

ERROR_MESSAGES = {
    TACOZ_ERR_IO: "I/O error (open/read/write/close/flush)",
    TACOZ_ERR_LIBZIP: "Reserved (historical); currently unused",
    TACOZ_ERR_INVALID_GHOST: "Ghost bytes malformed or unexpected",
    TACOZ_ERR_PARAM: "Invalid argument(s)",
}

class TacozipError(Exception):
    """Exception raised for tacozip library errors."""
    def __init__(self, code: int, message: str = None):
        self.code = code
        if message is None:
            message = ERROR_MESSAGES.get(code, f"Unknown error code: {code}")
        super().__init__(f"tacozip error {code}: {message}")

# Self-check functionality
try:
    from tacozip._self_check import _self_check
except ImportError:
    # Fallback if _self_check module doesn't exist
    def _self_check():
        _load_shared()  # This will raise if library can't be loaded

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

    # On Windows, also try some common variations
    if plat == "win32":
        # Try without extension
        for base in ["tacozip", "libtacozip"]:
            p = here / f"{base}.dll"
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

    # Fallback to system search paths (LD_LIBRARY_PATH/PATH)
    for envdir in os.getenv("LD_LIBRARY_PATH", "").split(":") + os.getenv("PATH", "").split(os.pathsep):
        if envdir:
            for n in names:
                cand = Path(envdir) / n
                if cand.exists():
                    try:
                        return ctypes.CDLL(str(cand))
                    except OSError as e:
                        print(f"Failed to load {cand}: {e}")
                        continue

    raise OSError(f"tacozip shared library not found. Searched for: {names}")

# Load the shared library
_lib = _load_shared()

# Define C structures
from ctypes import c_char_p, c_size_t, c_uint64, c_int, c_uint8, Structure, POINTER

class TacoMetaPtr(Structure):
    """Single metadata pointer (legacy)."""
    _fields_ = [("offset", c_uint64), ("length", c_uint64)]

class TacoMetaEntry(Structure):
    """Single metadata entry."""
    _fields_ = [("offset", c_uint64), ("length", c_uint64)]

class TacoMetaArray(Structure):
    """Array of up to 7 metadata entries."""
    _fields_ = [
        ("count", c_uint8),
        ("entries", TacoMetaEntry * 7),
    ]

# --- Legacy API function signatures ---
_lib.tacozip_create.argtypes = [
    c_char_p,                  # zip_path
    POINTER(c_char_p),         # src_files
    POINTER(c_char_p),         # arc_files
    c_size_t,                  # num_files
    c_uint64,                  # meta_offset
    c_uint64,                  # meta_length
]
_lib.tacozip_create.restype = c_int

_lib.tacozip_read_ghost.argtypes = [c_char_p, POINTER(TacoMetaPtr)]
_lib.tacozip_read_ghost.restype = c_int

_lib.tacozip_update_ghost.argtypes = [c_char_p, c_uint64, c_uint64]
_lib.tacozip_update_ghost.restype = c_int

# --- Multi-parquet API function signatures ---
_lib.tacozip_create_multi.argtypes = [
    c_char_p,                    # zip_path
    POINTER(c_char_p),           # src_files
    POINTER(c_char_p),           # arc_files
    c_size_t,                    # num_files
    POINTER(c_uint64),           # meta_offsets
    POINTER(c_uint64),           # meta_lengths
    c_size_t,                    # array_size
]
_lib.tacozip_create_multi.restype = c_int

_lib.tacozip_read_ghost_multi.argtypes = [
    c_char_p,                    # zip_path
    POINTER(TacoMetaArray),      # out
]
_lib.tacozip_read_ghost_multi.restype = c_int

_lib.tacozip_update_ghost_multi.argtypes = [
    c_char_p,                    # zip_path
    POINTER(c_uint64),           # meta_offsets
    POINTER(c_uint64),           # meta_lengths
    c_size_t,                    # array_size
]
_lib.tacozip_update_ghost_multi.restype = c_int

# Helper functions
def _check_result(result: int):
    """Check C function result and raise exception if error."""
    if result != TACOZ_OK:
        raise TacozipError(result)

def _prepare_string_array(strings: List[str]) -> Tuple[ctypes.Array, List[bytes]]:
    """Convert Python strings to C string array."""
    # Keep references to avoid garbage collection
    byte_strings = [s.encode('utf-8') for s in strings]
    string_array = (c_char_p * len(byte_strings))()
    for i, bs in enumerate(byte_strings):
        string_array[i] = bs
    return string_array, byte_strings

def _prepare_uint64_array(values: List[int], size: int = 7) -> ctypes.Array:
    """Convert Python list to C uint64 array."""
    if len(values) > size:
        raise ValueError(f"Too many values: {len(values)} > {size}")
    
    # Pad with zeros if needed
    padded_values = values + [0] * (size - len(values))
    return (c_uint64 * size)(*padded_values)

# ============================================================================
#                               LEGACY API
# ============================================================================

def create(zip_path: str, src_files: List[str], arc_files: List[str], 
          meta_offset: int = 0, meta_length: int = 0) -> int:
    """
    Create a ZIP64 archive with single metadata entry (legacy API).
    
    Args:
        zip_path: Output path for the archive
        src_files: List of source file paths
        arc_files: List of archive names
        meta_offset: Metadata offset (default: 0)
        meta_length: Metadata length (default: 0)
        
    Returns:
        0 on success, negative error code on failure
        
    Note:
        This is the legacy API. Consider using tacozip_create_multi() 
        for new code that needs multiple metadata entries.
    """
    if len(src_files) != len(arc_files):
        raise ValueError("src_files and arc_files must be same length")
    
    arr_src = (c_char_p * len(src_files))(*[s.encode("utf-8") for s in src_files])
    arr_arc = (c_char_p * len(arc_files))(*[s.encode("utf-8") for s in arc_files])
    
    return int(_lib.tacozip_create(
        zip_path.encode("utf-8"), arr_src, arr_arc,
        c_size_t(len(src_files)), c_uint64(meta_offset), c_uint64(meta_length)
    ))

def read_ghost(zip_path: str) -> Tuple[int, int, int]:
    """
    Read first metadata entry from ghost (legacy API).
    
    Args:
        zip_path: Path to the archive
        
    Returns:
        Tuple of (return_code, offset, length)
        
    Note:
        This is the legacy API. Consider using tacozip_read_ghost_multi()
        for new code that needs to read multiple metadata entries.
    """
    out = TacoMetaPtr()
    rc = _lib.tacozip_read_ghost(zip_path.encode("utf-8"), ctypes.byref(out))
    return int(rc), out.offset, out.length

def update_ghost(zip_path: str, new_offset: int, new_length: int) -> int:
    """
    Update first metadata entry in ghost (legacy API).
    
    Args:
        zip_path: Path to the archive
        new_offset: New metadata offset
        new_length: New metadata length
        
    Returns:
        0 on success, negative error code on failure
        
    Note:
        This is the legacy API. Consider using tacozip_update_ghost_multi()
        for new code that needs to update multiple metadata entries.
    """
    return int(_lib.tacozip_update_ghost(
        zip_path.encode("utf-8"), c_uint64(new_offset), c_uint64(new_length)
    ))

# ============================================================================
#                            NEW MULTI-PARQUET API
# ============================================================================

def tacozip_create_multi(zip_path: str, src_files: List[str], arc_files: List[str],
                        meta_offsets: List[int], meta_lengths: List[int]) -> None:
    """
    Create a ZIP64 archive with up to 7 metadata entries.
    
    Args:
        zip_path: Output path for the archive
        src_files: List of source file paths
        arc_files: List of archive names
        meta_offsets: List of up to 7 metadata offsets (pad with 0s for unused)
        meta_lengths: List of up to 7 metadata lengths (pad with 0s for unused)
        
    Raises:
        TacozipError: If creation fails
        ValueError: If parameters are invalid
        
    Example:
        tacozip_create_multi(
            "archive.zip",
            ["file1.txt", "file2.txt"],
            ["file1.txt", "file2.txt"],
            [1000, 2500, 0, 0, 0, 0, 0],  # 2 metadata entries
            [500,  750,  0, 0, 0, 0, 0]   # rest unused
        )
    """
    if len(src_files) != len(arc_files):
        raise ValueError("src_files and arc_files must have same length")
    
    if len(meta_offsets) != len(meta_lengths):
        raise ValueError("meta_offsets and meta_lengths must have same length")
    
    if len(meta_offsets) > 7:
        raise ValueError("Maximum 7 metadata entries supported")
    
    # Prepare C arrays
    src_array, src_bytes = _prepare_string_array(src_files)
    arc_array, arc_bytes = _prepare_string_array(arc_files)
    offset_array = _prepare_uint64_array(meta_offsets, 7)
    length_array = _prepare_uint64_array(meta_lengths, 7)
    
    # Call C function
    result = _lib.tacozip_create_multi(
        zip_path.encode('utf-8'),
        src_array,
        arc_array,
        len(src_files),
        offset_array,
        length_array,
        7
    )
    
    _check_result(result)

def tacozip_read_ghost_multi(zip_path: str) -> Tuple[int, List[Tuple[int, int]]]:
    """
    Read all metadata entries from the ghost.
    
    Args:
        zip_path: Path to the archive
        
    Returns:
        Tuple of (count, list of (offset, length) tuples)
        The list always contains 7 tuples, but only the first 'count' are valid.
        
    Raises:
        TacozipError: If reading fails
        
    Example:
        count, entries = tacozip_read_ghost_multi("archive.zip")
        for i in range(count):
            offset, length = entries[i]
            print(f"Entry {i+1}: offset={offset}, length={length}")
    """
    meta = TacoMetaArray()
    result = _lib.tacozip_read_ghost_multi(zip_path.encode('utf-8'), ctypes.byref(meta))
    _check_result(result)
    
    entries = []
    for i in range(7):  # Always return all 7 entries
        entries.append((meta.entries[i].offset, meta.entries[i].length))
    
    return meta.count, entries

def tacozip_update_ghost_multi(zip_path: str, meta_offsets: List[int], 
                              meta_lengths: List[int]) -> None:
    """
    Update all metadata entries in the ghost.
    
    Args:
        zip_path: Path to the archive
        meta_offsets: List of up to 7 metadata offsets
        meta_lengths: List of up to 7 metadata lengths
        
    Raises:
        TacozipError: If update fails
        ValueError: If parameters are invalid
        
    Example:
        # Update first 3 entries, leave rest as unused
        tacozip_update_ghost_multi(
            "archive.zip",
            [1500, 3000, 4500, 0, 0, 0, 0],
            [600,  800,  1300, 0, 0, 0, 0]
        )
    """
    if len(meta_offsets) != len(meta_lengths):
        raise ValueError("meta_offsets and meta_lengths must have same length")
    
    if len(meta_offsets) > 7:
        raise ValueError("Maximum 7 metadata entries supported")
    
    offset_array = _prepare_uint64_array(meta_offsets, 7)
    length_array = _prepare_uint64_array(meta_lengths, 7)
    
    result = _lib.tacozip_update_ghost_multi(
        zip_path.encode('utf-8'),
        offset_array,
        length_array,
        7
    )
    
    _check_result(result)

# ============================================================================
#                            CONVENIENCE FUNCTIONS
# ============================================================================

def get_version_info() -> dict:
    """
    Get detailed version and build information.
    
    Returns:
        Dictionary with version details
    """
    info = {
        "version": __version__,
        "author": __author__,
        "description": __description__,
        "url": __url__,
        "python_version": f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}",
        "platform": sys.platform,
    }
    
    # Add shared library info if available
    try:
        package_dir = Path(__file__).parent
        so_files = (list(package_dir.glob("*.so")) + 
                   list(package_dir.glob("*.dll")) + 
                   list(package_dir.glob("*.dylib")))
        
        if so_files:
            so_file = so_files[0]
            stat = so_file.stat()
            info["shared_library"] = {
                "path": str(so_file),
                "size": stat.st_size,
                "modified": stat.st_mtime,
            }
    except Exception:
        pass
    
    return info

def check_installation() -> bool:
    """
    Check if tacozip is properly installed and functional.
    
    Returns:
        True if installation is working, False otherwise
    """
    try:
        # Try to call _self_check
        _self_check()
        
        # Test basic functionality
        test_functions = [
            hasattr(_lib, 'tacozip_create'),
            hasattr(_lib, 'tacozip_read_ghost'),
            hasattr(_lib, 'tacozip_update_ghost'),
            hasattr(_lib, 'tacozip_create_multi'),
            hasattr(_lib, 'tacozip_read_ghost_multi'),
            hasattr(_lib, 'tacozip_update_ghost_multi'),
        ]
        
        if not all(test_functions):
            print("❌ Some required functions missing from shared library")
            return False
        
        print("✅ tacozip installation is working correctly")
        return True
        
    except Exception as e:
        print(f"❌ Error testing installation: {e}")
        return False

def info():
    """Print detailed package information."""
    info_dict = get_version_info()
    
    print(f"tacozip {info_dict['version']}")
    print(f"Author: {info_dict['author']}")
    print(f"Description: {info_dict['description']}")
    print(f"URL: {info_dict['url']}")
    print(f"Python: {info_dict['python_version']}")
    print(f"Platform: {info_dict['platform']}")
    
    if 'shared_library' in info_dict:
        lib_info = info_dict['shared_library']
        print(f"Shared library: {lib_info['path']} ({lib_info['size']} bytes)")
    else:
        print("Shared library: Not found or inaccessible")

def version() -> str:
    """Get the package version string."""
    return __version__

# ============================================================================
#                                EXPORTS
# ============================================================================

# Export public API
__all__ = [
    # Package metadata
    '__version__',
    '__author__',
    '__author_email__',
    '__description__',
    '__url__',
    '__license__',
    
    # Error handling
    'TacozipError',
    'TACOZ_OK',
    'TACOZ_ERR_IO',
    'TACOZ_ERR_LIBZIP',
    'TACOZ_ERR_INVALID_GHOST',
    'TACOZ_ERR_PARAM',
    
    # Legacy API (backward compatibility)
    'create',
    'read_ghost',
    'update_ghost',
    
    # New Multi-parquet API
    'tacozip_create_multi',
    'tacozip_read_ghost_multi',
    'tacozip_update_ghost_multi',
    
    # Utility functions
    'get_version_info',
    'check_installation',
    'info',
    'version',
]