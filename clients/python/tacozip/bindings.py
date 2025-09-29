import pathlib
import ctypes
from ctypes import c_char_p, c_size_t, c_uint64, c_int, c_uint8, Structure, POINTER
from typing import List, Tuple, Union

from .loader import get_library
from .config import TACOZ_OK, TACO_HEADER_MAX_ENTRIES
from .exceptions import TacozipError


# C Structures
class TacoMetaEntry(Structure):
    """Single metadata entry."""
    _fields_ = [("offset", c_uint64), ("length", c_uint64)]


class TacoMetaArray(Structure):
    """Array of up to 7 metadata entries."""
    _fields_ = [
        ("count", c_uint8),
        ("entries", TacoMetaEntry * TACO_HEADER_MAX_ENTRIES),
    ]


class TacoAppendEntry(Structure):
    """Entry for append operations (single or batch)."""
    _fields_ = [
        ("src_path", c_char_p),
        ("arc_name", c_char_p),
    ]


# Global library instance
_lib = get_library()

# Setup function signatures
_lib.tacozip_get_version.argtypes = []
_lib.tacozip_get_version.restype = c_char_p

# Low-level API
_lib.tacozip_parse_header.argtypes = [
    ctypes.c_void_p, c_size_t, POINTER(TacoMetaArray)
]
_lib.tacozip_parse_header.restype = c_int

# Convenience API
_lib.tacozip_read_header.argtypes = [
    c_char_p, POINTER(TacoMetaArray)
]
_lib.tacozip_read_header.restype = c_int

_lib.tacozip_create.argtypes = [
    c_char_p, POINTER(c_char_p), POINTER(c_char_p),
    c_size_t, POINTER(TacoMetaArray)
]
_lib.tacozip_create.restype = c_int

_lib.tacozip_update_header.argtypes = [
    c_char_p, POINTER(TacoMetaArray)
]
_lib.tacozip_update_header.restype = c_int

_lib.tacozip_append_files.argtypes = [
    c_char_p, POINTER(TacoAppendEntry), c_size_t
]
_lib.tacozip_append_files.restype = c_int

_lib.tacozip_replace_file.argtypes = [c_char_p, c_char_p, c_char_p]
_lib.tacozip_replace_file.restype = c_int

_lib.tacozip_trim_from.argtypes = [c_char_p, c_char_p]
_lib.tacozip_trim_from.restype = c_int


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


def _prepare_meta_array(entries: List[Tuple[int, int]]) -> TacoMetaArray:
    """Convert Python entries list to C TacoMetaArray structure."""
    if len(entries) > TACO_HEADER_MAX_ENTRIES:
        raise ValueError(f"Too many entries: {len(entries)} > {TACO_HEADER_MAX_ENTRIES}")
    
    meta = TacoMetaArray()
    
    # Count valid entries (non-zero pairs)
    valid_count = 0
    for offset, length in entries:
        if offset != 0 or length != 0:
            valid_count += 1
    
    meta.count = valid_count
    
    # Fill all 7 entries (pad with zeros if needed)
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
    entries = []
    
    for i in range(meta.count):
        entries.append((meta.entries[i].offset, meta.entries[i].length))
    
    return entries


def _prepare_append_entries(entries: List[Tuple[str, str]]) -> Tuple[ctypes.Array, List[bytes]]:
    """Convert list of (src_path, arc_name) tuples to C append entry array."""
    if not entries:
        raise ValueError("Must provide at least one entry to append")
    
    # Keep references to byte strings to prevent garbage collection
    byte_strings = []
    entry_array = (TacoAppendEntry * len(entries))()
    
    for i, (src_path, arc_name) in enumerate(entries):
        src_bytes = str(pathlib.Path(src_path).resolve()).encode('utf-8')
        arc_bytes = arc_name.encode('utf-8')
        
        byte_strings.extend([src_bytes, arc_bytes])
        
        entry_array[i].src_path = src_bytes
        entry_array[i].arc_name = arc_bytes
    
    return entry_array, byte_strings


def _normalize_inputs(src_files: List[Union[str, pathlib.Path]], 
                          arc_files: List[str] = None) -> Tuple[List[str], List[str]]:
    """Input normalization with minimal validation."""
    
    # Convert to strings, no heavy validation
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


# API functions with TACO_HEADER
def create(zip_path: str, src_files: List[Union[str, pathlib.Path]], 
           arc_files: List[str] = None, entries: List[Tuple[int, int]] = None):
    """Create archive with up to 7 metadata entries in TACO header."""
    
    # Default entries
    if entries is None:
        entries = [(0, 0)]
    
    # Minimal output validation
    validated_zip_path = _minimal_output_check(zip_path)
    
    # Fast input normalization
    normalized_src, normalized_arc = _normalize_inputs(src_files, arc_files)
    
    # Prepare arrays
    src_array, src_bytes = _prepare_string_array(normalized_src)
    arc_array, arc_bytes = _prepare_string_array(normalized_arc)
    meta = _prepare_meta_array(entries)
    
    print(f"Creating archive with {len(normalized_src)} files...")
    
    # Call C function
    result = _lib.tacozip_create(
        validated_zip_path.encode('utf-8'), src_array, arc_array,
        len(normalized_src), ctypes.byref(meta)
    )
    
    _check_result(result)
    
    try:
        archive_size = pathlib.Path(validated_zip_path).stat().st_size
        print(f"Archive: {validated_zip_path} ({archive_size:,} bytes)")
    except:
        print(f"Archive created: {validated_zip_path}")


def update_header(zip_path: str, entries: List[Tuple[int, int]]):
    """Update all metadata entries in TACO header."""
    meta = _prepare_meta_array(entries)
    
    result = _lib.tacozip_update_header(
        zip_path.encode('utf-8'), ctypes.byref(meta)
    )
    
    _check_result(result)


def read_header(source: Union[str, bytes, pathlib.Path]) -> List[Tuple[int, int]]:
    """Read all metadata entries from TACO header.
    
    Args:
        source: Either a file path (str/Path) OR bytes buffer (157+ bytes)
        
    Returns:
        List of (offset, length) tuples containing the metadata entries
        
    Examples:
        # From file
        entries = read_header("archive.taco")
        
        # From bytes (HTTP, S3, etc)
        import requests
        r = requests.get("https://cdn.com/data.taco", headers={"Range": "bytes=0-199"})
        entries = read_header(r.content)
        
        # From S3
        import boto3
        s3 = boto3.client('s3')
        obj = s3.get_object(Bucket='bucket', Key='data.taco', Range='bytes=0-199')
        entries = read_header(obj['Body'].read())
    """
    meta = TacoMetaArray()
    
    if isinstance(source, bytes):
        # Parse from buffer
        if len(source) < 157:
            raise ValueError(f"Buffer too small: {len(source)} < 157")
        
        buffer_ptr = ctypes.create_string_buffer(source, len(source))
        
        result = _lib.tacozip_parse_header(
            ctypes.cast(buffer_ptr, ctypes.c_void_p),
            len(source),
            ctypes.byref(meta)
        )
    else:
        # Read from file
        zip_path = str(source)
        result = _lib.tacozip_read_header(
            zip_path.encode('utf-8'), 
            ctypes.byref(meta)
        )
    
    _check_result(result)
    
    return _extract_meta_entries(meta)


def append_files(zip_path: str, entries: List[Tuple[str, str]]):
    """Append one or more files to an existing TACO archive.
    
    Args:
        zip_path: Path to existing TACO archive
        entries: List of (src_path, arc_name) tuples
        
    Examples:
        # Single file
        append_files("archive.taco", [("/path/to/file.bin", "data/file.bin")])
        
        # Multiple files
        append_files("archive.taco", [
            ("/path/to/file1.bin", "data/file1.bin"),
            ("/path/to/file2.bin", "data/file2.bin"),
            ("/path/to/file3.bin", "data/file3.bin")
        ])
    """
    if not entries:
        raise ValueError("Must provide at least one entry to append")
    
    # Prepare entry array
    entry_array, byte_strings = _prepare_append_entries(entries)
    
    # Call C function
    result = _lib.tacozip_append_files(
        zip_path.encode('utf-8'),
        entry_array,
        len(entries)
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


def trim_from(zip_path, target):
    """Trim archive from target to end."""
    # Convert Path objects to strings first
    zip_path_str = str(zip_path)
    target_str = str(target)
    
    zip_path_bytes = zip_path_str.encode('utf-8')
    target_bytes = target_str.encode('utf-8')
    
    result = _lib.tacozip_trim_from(zip_path_bytes, target_bytes)
    _check_result(result)


def get_library_version() -> str:
    """Get the C library version string."""
    version_bytes = _lib.tacozip_get_version()
    return version_bytes.decode('utf-8') if version_bytes else "unknown"