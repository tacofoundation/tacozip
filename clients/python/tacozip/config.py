"""Configuration constants for tacozip."""

# Error codes from C library
TACOZ_OK = 0
TACOZ_ERR_IO = -1
TACOZ_ERR_LIBZIP = -2
TACOZ_ERR_INVALID_GHOST = -3
TACOZ_ERR_PARAM = -4
TACOZ_ERR_NOT_FOUND = -5

# Error messages
ERROR_MESSAGES = {
    TACOZ_ERR_IO: "I/O error (open/read/write/close/flush)",
    TACOZ_ERR_LIBZIP: "Reserved (historical); currently unused",
    TACOZ_ERR_INVALID_GHOST: "Ghost bytes malformed or unexpected",
    TACOZ_ERR_PARAM: "Invalid argument(s)",
    TACOZ_ERR_NOT_FOUND: "File not found in archive",
}

# TACO Ghost constants
TACO_GHOST_MAX_ENTRIES = 7
TACO_GHOST_SIZE = 160
TACO_GHOST_NAME = "TACO_GHOST"
TACO_GHOST_NAME_LEN = 10
TACO_GHOST_EXTRA_ID = 0x7454
TACO_GHOST_EXTRA_SIZE = 116

# Platform-specific library names
LIBRARY_NAMES = {
    "linux": ["libtacozip.so"],
    "darwin": ["libtacozip.dylib"],
    "win32": ["tacozip.dll", "libtacozip.dll"],
}