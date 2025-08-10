import ctypes
import sys
import os
from pathlib import Path


try:
    from tacozip._self_check import _self_check
except ImportError:
    # Fallback if _self_check module doesn't exist
    def _self_check():
        _load_shared()  # This will raise if library can't be loaded


def _load_shared():
    plat = sys.platform
    names = []
    if plat.startswith("linux"):
        names = ["libtacozip.so"]
    elif plat == "darwin":
        names = ["libtacozip.dylib"]
    elif plat == "win32":
        names = ["tacozip.dll", "libtacozip.dll"]
    else:
        names = ["libtacozip.so"]

    here = Path(__file__).parent
    
    # First try to find library in the package directory
    for n in names:
        p = here / n
        if p.exists():
            try:
                return ctypes.CDLL(str(p))
            except OSError as e:
                print(f"Failed to load {p}: {e}")
                continue

    # On Windows, also try some common variations
    if plat == "win32":
        # Try without extension
        for base in ["tacozip", "libtacozip"]:
            p = here / f"{base}.dll"
            if p.exists():
                try:
                    return ctypes.CDLL(str(p))
                except OSError as e:
                    print(f"Failed to load {p}: {e}")
                    continue
    
    # Debug: list all files in the package directory
    print(f"Available files in {here}:")
    if here.exists():
        for item in here.iterdir():
            print(f"  - {item.name}")
    else:
        print(f"  Package directory {here} does not exist!")

    # Fallback to system search paths (LD_LIBRARY_PATH/PATH)
    for envdir in os.getenv("LD_LIBRARY_PATH", "").split(":") + os.getenv("PATH", "").split(os.pathsep):
        if envdir:
            for n in names:
                cand = Path(envdir) / n
                if cand.exists():
                    try:
                        return ctypes.CDLL(str(cand))
                    except OSError as e:
                        print(f"Failed to load {cand}: {e}")
                        continue

    raise OSError(f"tacozip shared library not found. Searched for: {names}")

_lib = _load_shared()

# --- ctypes signatures (keep in sync with tacozip.h) ---
from ctypes import c_char_p, c_size_t, c_uint64, c_int, Structure, POINTER

class TacoMetaPtr(Structure):
    _fields_ = [("offset", c_uint64), ("length", c_uint64)]

_lib.tacozip_create.argtypes = [
    c_char_p,                  # zip_path
    POINTER(c_char_p),         # src_files
    POINTER(c_char_p),         # arc_files
    c_size_t,                  # num_files
    c_uint64,                  # meta_offset
    c_uint64,                  # meta_length
]
_lib.tacozip_create.restype = c_int

_lib.tacozip_read_ghost.argtypes = [c_char_p, POINTER(TacoMetaPtr)]
_lib.tacozip_read_ghost.restype = c_int

_lib.tacozip_update_ghost.argtypes = [c_char_p, c_uint64, c_uint64]
_lib.tacozip_update_ghost.restype = c_int

def create(zip_path, src_files, arc_files, meta_offset=0, meta_length=0):
    if len(src_files) != len(arc_files):
        raise ValueError("src_files and arc_files must be same length")
    arr_src = (c_char_p * len(src_files))(*[s.encode("utf-8") for s in src_files])
    arr_arc = (c_char_p * len(arc_files))(*[s.encode("utf-8") for s in arc_files])
    return int(_lib.tacozip_create(
        zip_path.encode("utf-8"), arr_src, arr_arc,
        c_size_t(len(src_files)), c_uint64(meta_offset), c_uint64(meta_length)
    ))

def read_ghost(zip_path):
    out = TacoMetaPtr()
    rc = _lib.tacozip_read_ghost(zip_path.encode("utf-8"), ctypes.byref(out))
    return int(rc), out.offset, out.length

def update_ghost(zip_path, new_offset, new_length):
    return int(_lib.tacozip_update_ghost(zip_path.encode("utf-8"), c_uint64(new_offset), c_uint64(new_length)))