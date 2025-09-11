"""Configuration constants for tacozip."""

# Error codes from C library
TACOZ_OK = 0
TACOZ_ERR_IO = -1
TACOZ_ERR_LIBZIP = -2
TACOZ_ERR_INVALID_HEADER = -3
TACOZ_ERR_PARAM = -4
TACOZ_ERR_NOT_FOUND = -5
TACOZ_ERR_EXISTS = -6

# Error messages
ERROR_MESSAGES = {
    TACOZ_ERR_IO: "I/O error (open/read/write/close/flush)",
    TACOZ_ERR_LIBZIP: "Reserved (historical); currently unused",
    TACOZ_ERR_INVALID_HEADER: "Header bytes malformed or unexpected",
    TACOZ_ERR_PARAM: "Invalid argument(s)",
    TACOZ_ERR_NOT_FOUND: "File not found in archive",
    TACOZ_ERR_EXISTS: "File already exists in archive",
}

# TACO Header constants (updated from TACO Ghost)
TACO_HEADER_MAX_ENTRIES = 7
TACO_HEADER_SIZE = 161  # Updated: 30(LFH) + 11(filename) + 0(extra) + 116(payload) + 4(alignment)
TACO_HEADER_NAME = "TACO_HEADER"
TACO_HEADER_NAME_LEN = 11
TACO_HEADER_EXTRA_ID = 0x7454
TACO_HEADER_PAYLOAD_SIZE = 116

# Platform-specific library names
LIBRARY_NAMES = {
    "linux": ["libtacozip.so"],
    "darwin": ["libtacozip.dylib"],
    "win32": ["tacozip.dll", "libtacozip.dll"],
}