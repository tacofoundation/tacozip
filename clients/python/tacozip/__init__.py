from .version import __version__
from .config import (
    TACOZ_OK,
    TACOZ_ERR_IO,
    TACOZ_ERR_LIBZIP,
    TACOZ_ERR_INVALID_HEADER,
    TACOZ_ERR_PARAM,
    TACOZ_ERR_NOT_FOUND,
    TACOZ_ERR_EXISTS,
    TACOZ_ERR_TOO_LARGE,
    TACO_HEADER_MAX_ENTRIES,
    TACOZIP_FORMAT_UNKNOWN,
    TACOZIP_FORMAT_ZIP32,
    TACOZIP_FORMAT_ZIP64,
    TACOZIP_VALIDATE_QUICK,
    TACOZIP_VALIDATE_NORMAL,
    TACOZIP_VALIDATE_DEEP,
    TACOZ_VALID,
    TACOZ_INVALID_NOT_ZIP,
    TACOZ_INVALID_NO_TACO,
    TACOZ_INVALID_HEADER_SIZE,
    TACOZ_INVALID_META_COUNT,
    TACOZ_INVALID_FILE_SIZE,
    TACOZ_INVALID_NO_EOCD,
    TACOZ_INVALID_CD_OFFSET,
    TACOZ_INVALID_NO_CD_ENTRY,
    TACOZ_INVALID_REORDERED,
    TACOZ_INVALID_CRC_LFH,
    TACOZ_INVALID_CRC_CD,
)
from .exceptions import TacozipError
from .loader import self_check

# Import API from bindings
from .bindings import (
    create,
    update_header,
    read_header,
    detect_format,
    validate,
    get_library_version,
)

# Package metadata
__author__ = "Cesar Aybar"
__author_email__ = "cesar.aybar@uv.es"
__description__ = "Regular ZIP (STORE-only) writer with libzip backend and TACO Header at byte 0 (see TACO spec)."
__url__ = "https://github.com/tacofoundation/tacozip"
__license__ = "MIT"
__tacozip_version__ = get_library_version()

# Export public API
__all__ = [
    # Version
    "__version__",
    "__tacozip_version__",
    "__author__",
    "__author_email__",
    "__description__",
    "__url__",
    "__license__",
    # loader
    "self_check",
    # Constants
    "TACOZ_OK",
    "TACOZ_ERR_IO",
    "TACOZ_ERR_LIBZIP",
    "TACOZ_ERR_INVALID_HEADER",
    "TACOZ_ERR_PARAM",
    "TACOZ_ERR_NOT_FOUND",
    "TACOZ_ERR_EXISTS",
    "TACOZ_ERR_TOO_LARGE",
    "TACO_HEADER_MAX_ENTRIES",
    # Format detection
    "TACOZIP_FORMAT_UNKNOWN",
    "TACOZIP_FORMAT_ZIP32",
    "TACOZIP_FORMAT_ZIP64",
    # Validation levels
    "TACOZIP_VALIDATE_QUICK",
    "TACOZIP_VALIDATE_NORMAL",
    "TACOZIP_VALIDATE_DEEP",
    # Validation results
    "TACOZ_VALID",
    "TACOZ_INVALID_NOT_ZIP",
    "TACOZ_INVALID_NO_TACO",
    "TACOZ_INVALID_HEADER_SIZE",
    "TACOZ_INVALID_META_COUNT",
    "TACOZ_INVALID_FILE_SIZE",
    "TACOZ_INVALID_NO_EOCD",
    "TACOZ_INVALID_CD_OFFSET",
    "TACOZ_INVALID_NO_CD_ENTRY",
    "TACOZ_INVALID_REORDERED",
    "TACOZ_INVALID_CRC_LFH",
    "TACOZ_INVALID_CRC_CD",
    # Exceptions
    "TacozipError",
    # Core API
    "create",
    "update_header",
    "read_header",
    "detect_format",
    "validate",
    "get_library_version",
]
