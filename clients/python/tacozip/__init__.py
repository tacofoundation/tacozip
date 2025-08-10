"""
tacozip - High-performance ZIP64 writer with TACO Ghost metadata support.

This package provides both legacy and multi-parquet APIs for creating ZIP64 archives
with specialized metadata support.
"""

"""
tacozip - High-performance ZIP64 writer with TACO Ghost metadata support.
"""

from .version import __version__
from .config import *
from .exceptions import TacozipError
from .loader import self_check

# Import APIs directly from bindings
from .bindings import (
    create, read_ghost, update_ghost,
    create_multi, read_ghost_multi, update_ghost_multi
)

# Package metadata
__author__ = "Cesar Aybar"
__author_email__ = "csaybar@gmail.com"
__description__ = "TACO ZIP: ZIP64 archive with TACO Ghost supporting up to 7 metadata entries"
__url__ = "https://github.com/csaybar/tacozip"
__license__ = "MIT"

# Export public API
__all__ = [
    # Version
    "__version__",
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
    "TACOZ_ERR_INVALID_GHOST",
    "TACOZ_ERR_PARAM",
    "TACO_GHOST_MAX_ENTRIES",
    
    # Exceptions
    "TacozipError",
    
    # Legacy API
    "create",
    "read_ghost", 
    "update_ghost",
    
    # Multi-parquet API
    "create_multi",
    "read_ghost_multi",
    "update_ghost_multi",
]


