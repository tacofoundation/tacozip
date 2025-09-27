from .version import __version__
from .config import *
from .exceptions import TacozipError
from .loader import self_check

# Import simplified API from bindings
from .bindings import (
    create, update_header, append_files, replace_file, get_library_version, read_header, trim_from
)

# Package metadata
__author__ = "Cesar Aybar"
__author_email__ = "cesar.aybar@uv.es"
__description__ = "TACO ZIP: ZIP64 archive with TACO Header supporting up to 7 metadata entries"
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
    "TACO_HEADER_MAX_ENTRIES",
    
    # Exceptions
    "TacozipError",
    
    # Core API
    "create",
    "update_header", 
    "append_files",
    "replace_file",
    "trim_from",
    "read_header",
    "get_library_version"
]