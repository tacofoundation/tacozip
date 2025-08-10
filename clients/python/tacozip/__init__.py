import ctypes
import sys
import os
from pathlib import Path


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
    for n in names:
        p = here / n
        if p.exists():
            return ctypes.CDLL(str(p))

    # Fallback to system search paths (LD_LIBRARY_PATH/PATH)
    for envdir in os.getenv("LD_LIBRARY_PATH", "").split(":") + os.getenv("PATH", "").split(os.pathsep):
        if envdir:
            for n in names:
                cand = Path(envdir) / n
                if cand.exists():
                    return ctypes.CDLL(str(cand))

    raise OSError("tacozip shared library not found")

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
