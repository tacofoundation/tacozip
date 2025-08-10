"""Custom exceptions for tacozip."""

from .config import ERROR_MESSAGES


class TacozipError(Exception):
    """Base exception for tacozip library errors."""
    
    def __init__(self, code: int, message: str = None):
        self.code = code
        if message is None:
            message = ERROR_MESSAGES.get(code, f"Unknown error code: {code}")
        super().__init__(f"tacozip error {code}: {message}")


class TacozipIOError(TacozipError):
    """I/O related errors."""
    pass


class TacozipValidationError(TacozipError):
    """Validation and parameter errors."""
    pass


class TacozipLibraryError(TacozipError):
    """Native library loading errors."""
    pass