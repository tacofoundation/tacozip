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
)
from .exceptions import TacozipError
from .loader import self_check
from .bindings import (
    create,
    update_header,
    read_header,
    get_library_version,
)

__author__ = "Cesar Aybar"
__author_email__ = "cesar.aybar@uv.es"
__description__ = "Regular ZIP (STORE-only) writer with libzip backend and TACO Header at byte 0."
__url__ = "https://github.com/tacofoundation/tacozip"
__license__ = "MIT"
__tacozip_version__ = get_library_version()

__all__ = [
    "__version__",
    "__tacozip_version__",
    "__author__",
    "__author_email__",
    "__description__",
    "__url__",
    "__license__",
    "self_check",
    "TACOZ_OK",
    "TACOZ_ERR_IO",
    "TACOZ_ERR_LIBZIP",
    "TACOZ_ERR_INVALID_HEADER",
    "TACOZ_ERR_PARAM",
    "TACOZ_ERR_NOT_FOUND",
    "TACOZ_ERR_EXISTS",
    "TACOZ_ERR_TOO_LARGE",
    "TACO_HEADER_MAX_ENTRIES",
    "TacozipError",
    "create",
    "update_header",
    "read_header",
    "get_library_version",
]