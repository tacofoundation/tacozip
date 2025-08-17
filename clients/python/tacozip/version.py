"""Version information for tacozip."""

# Try multiple version detection strategies
def _get_version() -> str:
    """Get package version using multiple fallback strategies."""
    # Strategy 1: Modern importlib.metadata (Python 3.8+)
    try:
        from importlib import metadata
        return metadata.version("tacozip")
    except Exception:
        pass
    
    # Strategy 2: Legacy pkg_resources
    try:
        import pkg_resources
        return pkg_resources.get_distribution("tacozip").version
    except Exception:
        pass
    
    # Strategy 3: Hardcoded fallback
    return "0.0.0"



__version__ = _get_version()