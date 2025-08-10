import ctypes
from ctypes import c_char_p, c_size_t, c_uint64, c_int, c_uint8, Structure, POINTER
from typing import List, Tuple

from .loader import get_library
from .config import TACOZ_OK, TACO_GHOST_MAX_ENTRIES
from .exceptions import TacozipError


# C Structures
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
        ("entries", TacoMetaEntry * TACO_GHOST_MAX_ENTRIES),
    ]


# Global library instance
_lib = get_library()

# Setup function signatures
_lib.tacozip_create.argtypes = [
    c_char_p, POINTER(c_char_p), POINTER(c_char_p), 
    c_size_t, c_uint64, c_uint64
]
_lib.tacozip_create.restype = c_int

_lib.tacozip_read_ghost.argtypes = [c_char_p, POINTER(TacoMetaPtr)]
_lib.tacozip_read_ghost.restype = c_int

_lib.tacozip_update_ghost.argtypes = [c_char_p, c_uint64, c_uint64]
_lib.tacozip_update_ghost.restype = c_int

_lib.tacozip_create_multi.argtypes = [
    c_char_p, POINTER(c_char_p), POINTER(c_char_p),
    c_size_t, POINTER(c_uint64), POINTER(c_uint64), c_size_t
]
_lib.tacozip_create_multi.restype = c_int

_lib.tacozip_read_ghost_multi.argtypes = [c_char_p, POINTER(TacoMetaArray)]
_lib.tacozip_read_ghost_multi.restype = c_int

_lib.tacozip_update_ghost_multi.argtypes = [
    c_char_p, POINTER(c_uint64), POINTER(c_uint64), c_size_t
]
_lib.tacozip_update_ghost_multi.restype = c_int


def _check_result(result: int):
    """Check C function result and raise exception if error."""
    if result != TACOZ_OK:
        raise TacozipError(result)


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
        raise ValueError(f"Too many values: {len(values)} > {size}")
    
    # Pad with zeros if needed
    padded_values = values + [0] * (size - len(values))
    return (c_uint64 * size)(*padded_values)


# Legacy API functions
def create(zip_path: str, src_files: List[str], arc_files: List[str], 
           meta_offset: int = 0, meta_length: int = 0) -> int:
    """Create archive with single metadata entry."""
    src_array, src_bytes = _prepare_string_array(src_files)
    arc_array, arc_bytes = _prepare_string_array(arc_files)
    
    return _lib.tacozip_create(
        zip_path.encode("utf-8"), src_array, arc_array,
        c_size_t(len(src_files)), c_uint64(meta_offset), c_uint64(meta_length)
    )


def read_ghost(zip_path: str) -> Tuple[int, int, int]:
    """Read first metadata entry from ghost."""
    out = TacoMetaPtr()
    rc = _lib.tacozip_read_ghost(zip_path.encode("utf-8"), ctypes.byref(out))
    return rc, out.offset, out.length


def update_ghost(zip_path: str, new_offset: int, new_length: int) -> int:
    """Update first metadata entry in ghost."""
    return _lib.tacozip_update_ghost(
        zip_path.encode("utf-8"), c_uint64(new_offset), c_uint64(new_length)
    )


# Multi-parquet API functions
def create_multi(zip_path: str, src_files: List[str], arc_files: List[str],
                        meta_offsets: List[int], meta_lengths: List[int]):
    """Create archive with multiple metadata entries."""
    src_array, src_bytes = _prepare_string_array(src_files)
    arc_array, arc_bytes = _prepare_string_array(arc_files)
    offset_array = _prepare_uint64_array(meta_offsets)
    length_array = _prepare_uint64_array(meta_lengths)
    
    result = _lib.tacozip_create_multi(
        zip_path.encode('utf-8'), src_array, arc_array,
        len(src_files), offset_array, length_array, TACO_GHOST_MAX_ENTRIES
    )
    
    _check_result(result)


def read_ghost_multi(zip_path: str) -> Tuple[int, List[Tuple[int, int]]]:
    """Read all metadata entries from ghost."""
    meta = TacoMetaArray()
    result = _lib.tacozip_read_ghost_multi(zip_path.encode('utf-8'), ctypes.byref(meta))
    _check_result(result)
    
    entries = []
    for i in range(TACO_GHOST_MAX_ENTRIES):
        entries.append((meta.entries[i].offset, meta.entries[i].length))
    
    return meta.count, entries


def update_ghost_multi(zip_path: str, meta_offsets: List[int], meta_lengths: List[int]):
    """Update all metadata entries in ghost."""
    offset_array = _prepare_uint64_array(meta_offsets)
    length_array = _prepare_uint64_array(meta_lengths)
    
    result = _lib.tacozip_update_ghost_multi(
        zip_path.encode('utf-8'), offset_array, length_array, TACO_GHOST_MAX_ENTRIES
    )
    
    _check_result(result)