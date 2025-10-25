import pathlib
import ctypes
from ctypes import c_char_p, c_size_t, c_uint64, c_int, c_uint8, Structure, POINTER
from typing import List, Tuple, Union

from .loader import get_library
from .config import (
    TACOZ_OK,
    TACO_HEADER_MAX_ENTRIES,
    TACOZIP_VALIDATE_NORMAL,
    VALIDATION_ERROR_MESSAGES,
)
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


# Global library instance
_lib = get_library()

# Setup function signatures
_lib.tacozip_get_version.argtypes = []
_lib.tacozip_get_version.restype = c_char_p

_lib.tacozip_parse_header.argtypes = [
    ctypes.c_void_p, c_size_t, POINTER(TacoMetaArray)
]
_lib.tacozip_parse_header.restype = c_int

_lib.tacozip_serialize_header.argtypes = [
    POINTER(TacoMetaArray), ctypes.c_void_p, c_size_t
]
_lib.tacozip_serialize_header.restype = c_int

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

_lib.tacozip_detect_format.argtypes = [c_char_p]
_lib.tacozip_detect_format.restype = c_int

_lib.tacozip_validate.argtypes = [c_char_p, c_int]
_lib.tacozip_validate.restype = c_int


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


# ============================================================================
#                           PUBLIC API
# ============================================================================

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


def detect_format(zip_path: str) -> int:
    """Detect if archive is ZIP32 or ZIP64.
    
    Args:
        zip_path: Path to archive
        
    Returns:
        TACOZIP_FORMAT_ZIP32 (1), TACOZIP_FORMAT_ZIP64 (2), or TACOZIP_FORMAT_UNKNOWN (0)
        
    Example:
        from tacozip import detect_format, TACOZIP_FORMAT_ZIP64
        
        format_type = detect_format("archive.taco")
        if format_type == TACOZIP_FORMAT_ZIP64:
            print("ZIP64 format detected")
    """
    result = _lib.tacozip_detect_format(zip_path.encode('utf-8'))
    return result


def validate(zip_path: str, level: int = TACOZIP_VALIDATE_NORMAL) -> int:
    """Validate TACO archive integrity.
    
    Args:
        zip_path: Path to archive
        level: Validation level (QUICK=0, NORMAL=1, DEEP=2)
        
    Returns:
        TACOZ_VALID (0) if valid, or negative error code
        
    Example:
        from tacozip import validate, TACOZ_VALID, TACOZ_INVALID_NO_TACO
        from tacozip.config import VALIDATION_ERROR_MESSAGES
        
        result = validate("archive.taco")
        if result == TACOZ_VALID:
            print("Archive is valid")
        elif result == TACOZ_INVALID_NO_TACO:
            print("ERROR: File was modified by external tool!")
        else:
            print(f"Validation failed: {VALIDATION_ERROR_MESSAGES.get(result, 'Unknown error')}")
    """
    result = _lib.tacozip_validate(zip_path.encode('utf-8'), level)
    return result


def get_library_version() -> str:
    """Get the C library version string."""
    version_bytes = _lib.tacozip_get_version()
    return version_bytes.decode('utf-8') if version_bytes else "unknown"