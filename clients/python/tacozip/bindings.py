import pathlib
import ctypes
from ctypes import c_char_p, c_size_t, c_uint64, c_int, c_uint8, Structure, POINTER
from typing import List, Tuple, Union

from .loader import get_library
from .config import TACOZ_OK, TACO_HEADER_MAX_ENTRIES
from .exceptions import TacozipError


class TacoMetaEntry(Structure):
    """Single metadata entry."""
    _fields_ = [("offset", c_uint64), ("length", c_uint64)]


class TacoMetaArray(Structure):
    """Array of up to 7 metadata entries."""
    _fields_ = [
        ("count", c_uint8),
        ("entries", TacoMetaEntry * TACO_HEADER_MAX_ENTRIES),
    ]


_lib = get_library()

# Function signatures
_lib.tacozip_get_version.argtypes = []
_lib.tacozip_get_version.restype = c_char_p

_lib.tacozip_parse_header.argtypes = [ctypes.c_void_p, c_size_t, POINTER(TacoMetaArray)]
_lib.tacozip_parse_header.restype = c_int

_lib.tacozip_serialize_header.argtypes = [
    POINTER(TacoMetaArray),
    ctypes.c_void_p,
    c_size_t,
]
_lib.tacozip_serialize_header.restype = c_int

_lib.tacozip_read_header.argtypes = [c_char_p, POINTER(TacoMetaArray)]
_lib.tacozip_read_header.restype = c_int

_lib.tacozip_create.argtypes = [
    c_char_p,
    POINTER(c_char_p),
    POINTER(c_char_p),
    c_size_t,
    POINTER(TacoMetaArray),
]
_lib.tacozip_create.restype = c_int

_lib.tacozip_update_header.argtypes = [c_char_p, POINTER(TacoMetaArray)]
_lib.tacozip_update_header.restype = c_int


def _check_result(result: int):
    """Check C function result and raise exception if error."""
    if result != TACOZ_OK:
        raise TacozipError(result)


def _prepare_meta_array(entries: List[Tuple[int, int]]) -> TacoMetaArray:
    """Convert Python entries list to C TacoMetaArray structure."""
    if len(entries) > TACO_HEADER_MAX_ENTRIES:
        raise ValueError(
            f"Too many entries: {len(entries)} > {TACO_HEADER_MAX_ENTRIES}"
        )

    meta = TacoMetaArray()
    
    valid_count = sum(1 for offset, length in entries if offset != 0 or length != 0)
    meta.count = valid_count

    for i in range(TACO_HEADER_MAX_ENTRIES):
        if i < len(entries):
            meta.entries[i].offset = entries[i][0]
            meta.entries[i].length = entries[i][1]
        else:
            meta.entries[i].offset = 0
            meta.entries[i].length = 0

    return meta


def _extract_meta_entries(meta: TacoMetaArray) -> List[Tuple[int, int]]:
    """Extract Python entries list from C TacoMetaArray structure."""
    return [(meta.entries[i].offset, meta.entries[i].length) for i in range(meta.count)]


def create(
    zip_path: str,
    src_files: List[Union[str, pathlib.Path]],
    arc_files: List[str] = None,
    entries: List[Tuple[int, int]] = None,
):
    """Create archive with up to 7 metadata entries in TACO header."""
    
    if entries is None:
        entries = [(0, 0)]

    # Convert paths to absolute strings
    src_strings = [str(pathlib.Path(f).resolve()) for f in src_files]
    
    # Generate archive names if not provided
    if arc_files is None:
        arc_strings = [pathlib.Path(f).name for f in src_strings]
    else:
        if len(arc_files) != len(src_strings):
            raise ValueError(
                f"Archive names count ({len(arc_files)}) must match source files count ({len(src_strings)})"
            )
        arc_strings = arc_files

    # Prepare C arrays
    src_bytes = [s.encode("utf-8") for s in src_strings]
    arc_bytes = [s.encode("utf-8") for s in arc_strings]
    
    src_array = (c_char_p * len(src_bytes))(*src_bytes)
    arc_array = (c_char_p * len(arc_bytes))(*arc_bytes)
    
    meta = _prepare_meta_array(entries)
    
    zip_path_abs = str(pathlib.Path(zip_path).resolve())

    result = _lib.tacozip_create(
        zip_path_abs.encode("utf-8"),
        src_array,
        arc_array,
        len(src_strings),
        ctypes.byref(meta),
    )

    _check_result(result)


def update_header(zip_path: str, entries: List[Tuple[int, int]]):
    """Update all metadata entries in TACO header."""
    meta = _prepare_meta_array(entries)
    result = _lib.tacozip_update_header(zip_path.encode("utf-8"), ctypes.byref(meta))
    _check_result(result)


def read_header(source: Union[str, bytes, pathlib.Path]) -> List[Tuple[int, int]]:
    """Read all metadata entries from TACO header.

    Args:
        source: Either a file path (str/Path) OR bytes buffer (157+ bytes)

    Returns:
        List of (offset, length) tuples containing the metadata entries
    """
    meta = TacoMetaArray()

    if isinstance(source, bytes):
        if len(source) < 157:
            raise ValueError(f"Buffer too small: {len(source)} < 157")

        buffer_ptr = ctypes.create_string_buffer(source, len(source))
        result = _lib.tacozip_parse_header(
            ctypes.cast(buffer_ptr, ctypes.c_void_p), len(source), ctypes.byref(meta)
        )
    else:
        zip_path = str(source)
        result = _lib.tacozip_read_header(zip_path.encode("utf-8"), ctypes.byref(meta))

    _check_result(result)
    return _extract_meta_entries(meta)


def get_library_version() -> str:
    """Get the C library version string."""
    version_bytes = _lib.tacozip_get_version()
    return version_bytes.decode("utf-8") if version_bytes else "unknown"