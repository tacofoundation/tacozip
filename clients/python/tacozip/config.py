"""Configuration constants for tacozip."""

# Error codes
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
    TACOZ_ERR_TOO_LARGE: "Archive too large",
}

# TACO Header constants
TACO_HEADER_MAX_ENTRIES = 7
TACO_HEADER_SIZE = 157
TACO_HEADER_NAME = "TACO_HEADER"
TACO_HEADER_NAME_LEN = 11
TACO_HEADER_PAYLOAD_SIZE = 116

# Platform-specific library names
LIBRARY_NAMES = {
    "linux": ["libtacozip.so"],
    "darwin": ["libtacozip.dylib"],
    "win32": ["tacozip.dll", "libtacozip.dll"],
}