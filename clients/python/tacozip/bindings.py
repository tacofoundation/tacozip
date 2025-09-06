import pathlib
import ctypes
from ctypes import c_char_p, c_size_t, c_uint64, c_int, c_uint8, Structure, POINTER
from typing import List, Tuple, Union

from .loader import get_library
from .config import TACOZ_OK, TACO_GHOST_MAX_ENTRIES
from .exceptions import TacozipError


# C Structures
class TacoMetaEntry(Structure):
    """Single metadata entry."""
    _fields_ = [("offset", c_uint64), ("length", c_uint64)]


class TacoMetaArray(Structure):
    """Array of up to 7 metadata entries."""
    _fields_ = [
        ("count", c_uint8),
        ("entries", TacoMetaEntry * TACO_GHOST_MAX_ENTRIES),
    ]


# Global library instance
_lib = get_library()

# Setup function signatures for simplified API
_lib.tacozip_get_version.argtypes = []
_lib.tacozip_get_version.restype = c_char_p

_lib.tacozip_create.argtypes = [
    c_char_p, POINTER(c_char_p), POINTER(c_char_p),
    c_size_t, POINTER(c_uint64), POINTER(c_uint64), c_size_t
]
_lib.tacozip_create.restype = c_int

_lib.tacozip_update_ghost.argtypes = [
    c_char_p, POINTER(c_uint64), POINTER(c_uint64), c_size_t
]
_lib.tacozip_update_ghost.restype = c_int

_lib.tacozip_append_file.argtypes = [c_char_p, c_char_p, c_char_p]
_lib.tacozip_append_file.restype = c_int

_lib.tacozip_replace_file.argtypes = [c_char_p, c_char_p, c_char_p]
_lib.tacozip_replace_file.restype = c_int


def _check_result(result: int):
    """Check C function result and raise exception if error."""
    if result != TACOZ_OK:
        raise TacozipError(result)


def _minimal_output_check(zip_path: str) -> str:
    """Minimal output path validation - only create parent dirs if needed."""
    zip_path = pathlib.Path(zip_path)
    
    # Only create parent directories if they don't exist
    if zip_path.parent != pathlib.Path('.') and not zip_path.parent.exists():
        zip_path.parent.mkdir(parents=True, exist_ok=True)
    
    return str(zip_path)


def _prepare_string_array(strings: List[str]) -> Tuple[ctypes.Array, List[bytes]]:
    """Convert Python strings to C string array."""
    byte_strings = [s.encode('utf-8') for s in strings]
    string_array = (c_char_p * len(byte_strings))()
    for i, bs in enumerate(byte_strings):
        string_array[i] = bs
    return string_array, byte_strings


def _prepare_uint64_array(values: List[int], size: int = TACO_GHOST_MAX_ENTRIES) -> ctypes.Array:
    """Convert Python list to C uint64 array."""
    if len(values) > size:
        raise ValueError(f"Too many metadata values: {len(values)} > {size}")
    
    # Pad with zeros if needed
    padded_values = values + [0] * (size - len(values))
    return (c_uint64 * size)(*padded_values)


def _fast_normalize_inputs(src_files: List[Union[str, pathlib.Path]], 
                          arc_files: List[str] = None) -> Tuple[List[str], List[str]]:
    """Fast input normalization with minimal validation."""
    
    # Convert to strings, no heavy validation
    if isinstance(src_files[0], pathlib.Path):
        normalized_src = [str(f.resolve()) for f in src_files]
    else:
        normalized_src = [str(pathlib.Path(f).resolve()) for f in src_files]
    
    # Handle archive names
    if arc_files is not None:
        if len(arc_files) != len(normalized_src):
            raise ValueError(f"Archive names count ({len(arc_files)}) must match source files count ({len(normalized_src)})")
        normalized_arc = arc_files
    else:
        # Auto-generate names quickly
        normalized_arc = [pathlib.Path(f).name for f in normalized_src]
    
    return normalized_src, normalized_arc


# Simplified API functions
def create(zip_path: str, src_files: List[Union[str, pathlib.Path]], 
           arc_files: List[str] = None, meta_offsets: List[int] = None, 
           meta_lengths: List[int] = None):
    """Create archive with up to 7 metadata entries. Unified API."""
    
    # Default metadata
    if meta_offsets is None:
        meta_offsets = [0]
    if meta_lengths is None:
        meta_lengths = [0]
    
    # Quick metadata validation
    if len(meta_offsets) != len(meta_lengths):
        raise ValueError(f"Metadata arrays must have same length")
    if len(meta_offsets) > TACO_GHOST_MAX_ENTRIES:
        raise ValueError(f"Too many metadata entries: {len(meta_offsets)} > {TACO_GHOST_MAX_ENTRIES}")
    
    # Minimal output validation
    validated_zip_path = _minimal_output_check(zip_path)
    
    # Fast input normalization
    normalized_src, normalized_arc = _fast_normalize_inputs(src_files, arc_files)
    
    # Prepare arrays
    src_array, src_bytes = _prepare_string_array(normalized_src)
    arc_array, arc_bytes = _prepare_string_array(normalized_arc)
    offset_array = _prepare_uint64_array(meta_offsets)
    length_array = _prepare_uint64_array(meta_lengths)
    
    print(f"📦 Creating archive with {len(normalized_src)} files...")
    
    # Call C function
    result = _lib.tacozip_create(
        validated_zip_path.encode('utf-8'), src_array, arc_array,
        len(normalized_src), offset_array, length_array, TACO_GHOST_MAX_ENTRIES
    )
    
    _check_result(result)
    
    try:
        archive_size = pathlib.Path(validated_zip_path).stat().st_size
        print(f"✅ Archive: {validated_zip_path} ({archive_size:,} bytes)")
    except:
        print(f"✅ Archive created: {validated_zip_path}")


def update_ghost(zip_path: str, meta_offsets: List[int], meta_lengths: List[int]):
    """Update all metadata entries in ghost."""
    if len(meta_offsets) != len(meta_lengths):
        raise ValueError("Metadata arrays must have same length")
    if len(meta_offsets) > TACO_GHOST_MAX_ENTRIES:
        raise ValueError(f"Too many metadata entries: {len(meta_offsets)} > {TACO_GHOST_MAX_ENTRIES}")
    
    offset_array = _prepare_uint64_array(meta_offsets)
    length_array = _prepare_uint64_array(meta_lengths)
    
    result = _lib.tacozip_update_ghost(
        zip_path.encode('utf-8'), offset_array, length_array, TACO_GHOST_MAX_ENTRIES
    )
    
    _check_result(result)


def append_file(zip_path: str, src_path: str, arc_name: str):
    """Append a new file to an existing TACO archive."""
    result = _lib.tacozip_append_file(
        zip_path.encode('utf-8'),
        src_path.encode('utf-8'),
        arc_name.encode('utf-8')
    )
    
    _check_result(result)


def replace_file(zip_path: str, file_name: str, new_src_path: str):
    """Replace a specific file in an existing TACO archive."""
    result = _lib.tacozip_replace_file(
        zip_path.encode('utf-8'),
        file_name.encode('utf-8'), 
        new_src_path.encode('utf-8')
    )
    
    _check_result(result)


def get_library_version() -> str:
    """Get the C library version string."""
    version_bytes = _lib.tacozip_get_version()
    return version_bytes.decode('utf-8') if version_bytes else "unknown"