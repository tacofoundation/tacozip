"""Configuration constants for tacozip."""

# Error codes from C library
TACOZ_OK = 0
TACOZ_ERR_IO = -1
TACOZ_ERR_LIBZIP = -2
TACOZ_ERR_INVALID_HEADER = -3
TACOZ_ERR_PARAM = -4
TACOZ_ERR_NOT_FOUND = -5
TACOZ_ERR_EXISTS = -6
TACOZ_ERR_TOO_LARGE = -7

# Error messages
ERROR_MESSAGES = {
    TACOZ_ERR_IO: "I/O error (open/read/write/close/flush)",
    TACOZ_ERR_LIBZIP: "An error occurred in libzip",
    TACOZ_ERR_INVALID_HEADER: "Header bytes malformed or unexpected",
    TACOZ_ERR_PARAM: "Invalid argument(s)",
    TACOZ_ERR_NOT_FOUND: "File not found in archive",
    TACOZ_ERR_EXISTS: "File already exists in archive",
    TACOZ_ERR_TOO_LARGE: "Archive too large"
}

# Format detection constants
TACOZIP_FORMAT_UNKNOWN = 0
TACOZIP_FORMAT_ZIP32 = 1
TACOZIP_FORMAT_ZIP64 = 2

# Validation levels
TACOZIP_VALIDATE_QUICK = 0   # Level 1: Header checks onlyS
TACOZIP_VALIDATE_NORMAL = 1  # Level 1+2: + Structure checks
TACOZIP_VALIDATE_DEEP = 2    # All levels: + CRC32 validation

# Validation result codes
TACOZ_VALID = 0

# Level 1 errors (Critical - header checks)
TACOZ_INVALID_NOT_ZIP = -10
TACOZ_INVALID_NO_TACO = -11
TACOZ_INVALID_HEADER_SIZE = -12
TACOZ_INVALID_META_COUNT = -13
TACOZ_INVALID_FILE_SIZE = -14

# Level 2 errors (Structure checks)
TACOZ_INVALID_NO_EOCD = -20
TACOZ_INVALID_CD_OFFSET = -21
TACOZ_INVALID_NO_CD_ENTRY = -22
TACOZ_INVALID_REORDERED = -23

# Level 3 errors (Deep validation)
TACOZ_INVALID_CRC_LFH = -30
TACOZ_INVALID_CRC_CD = -31

# Validation error messages
VALIDATION_ERROR_MESSAGES = {
    TACOZ_VALID: "Valid TACO archive",
    TACOZ_INVALID_NOT_ZIP: "Not a ZIP file (missing LFH signature)",
    TACOZ_INVALID_NO_TACO: "No TACO_HEADER at offset 0 (file modified by external tool)",
    TACOZ_INVALID_HEADER_SIZE: "Invalid header size (corrupted)",
    TACOZ_INVALID_META_COUNT: "Invalid metadata count (must be 0-7)",
    TACOZ_INVALID_FILE_SIZE: "File too small to be valid archive",
    TACOZ_INVALID_NO_EOCD: "No End of Central Directory record found",
    TACOZ_INVALID_CD_OFFSET: "Invalid Central Directory offset",
    TACOZ_INVALID_NO_CD_ENTRY: "TACO_HEADER not found in Central Directory",
    TACOZ_INVALID_REORDERED: "Archive entries reordered (CD doesn't point to offset 0)",
    TACOZ_INVALID_CRC_LFH: "CRC32 mismatch in Local File Header",
    TACOZ_INVALID_CRC_CD: "CRC32 mismatch in Central Directory",
}

# TACO Header constants
TACO_HEADER_MAX_ENTRIES = 7
TACO_HEADER_SIZE = 157  # 30(LFH) + 11(filename) + 116(payload)
TACO_HEADER_NAME = "TACO_HEADER"
TACO_HEADER_NAME_LEN = 11
TACO_HEADER_PAYLOAD_SIZE = 116

# Platform-specific library names
LIBRARY_NAMES = {
    "linux": ["libtacozip.so"],
    "darwin": ["libtacozip.dylib"],
    "win32": ["tacozip.dll", "libtacozip.dll"],
}