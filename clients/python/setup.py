import sys
from pathlib import Path
import shutil
import os

from setuptools import setup, Distribution
from setuptools.command.build_py import build_py as _build_py
from wheel.bdist_wheel import bdist_wheel as _bdist_wheel

class BinaryDistribution(Distribution):
    def has_ext_modules(self):  # tells setuptools this is a binary wheel
        return True

class bdist_wheel(_bdist_wheel):
    def finalize_options(self):
        super().finalize_options()
        self.root_is_pure = False  # put files under platlib, not purelib


class BuildTacozipExt(_build_py):
    """Ensure the compiled tacozip shared library is bundled."""

    def run(self):
        super().run()
        self.copy_built_library()

    def copy_built_library(self):
        pkg_dir = Path(self.get_package_dir("tacozip"))
        pkg_dir.mkdir(parents=True, exist_ok=True)

        # Check if library already exists in package dir (from CIBW_BEFORE_ALL)
        if sys.platform == "win32":
            lib_names = ["tacozip.dll", "libtacozip.dll"]
        elif sys.platform == "darwin":
            lib_names = ["libtacozip.dylib"]
        else:
            lib_names = ["libtacozip.so"]

        # First check if library is already in the package directory
        for lib_name in lib_names:
            if (pkg_dir / lib_name).exists():
                print(f"Found library already in package: {pkg_dir / lib_name}")
                return

        # If not found in package, try to find in build directory
        root = Path(__file__).resolve().parents[2]
        build_dir = root / "build" / "release"
        
        if not build_dir.exists():
            raise FileNotFoundError(f"Build directory not found: {build_dir}")

        if sys.platform == "win32":
            patterns = ["tacozip*.dll", "libtacozip*.dll"]
            dest_name = "tacozip.dll"
        elif sys.platform == "darwin":
            patterns = ["libtacozip*.dylib"]
            dest_name = "libtacozip.dylib"
        else:
            patterns = ["libtacozip*.so*"]
            dest_name = "libtacozip.so"

        for pat in patterns:
            cand = list(build_dir.glob(f"**/{pat}"))
            if cand:
                src = cand[0]
                dest = pkg_dir / dest_name
                print(f"Copying library: {src} -> {dest}")
                shutil.copy2(src, dest)
                return

        raise FileNotFoundError(
            f"tacozip shared library not found in {build_dir}. "
            f"Expected one of: {patterns}"
        )

def _lib_name():
    if sys.platform.startswith("win"):
        return "tacozip.dll"
    elif sys.platform == "darwin":
        return "libtacozip.dylib"
    else:
        return "libtacozip.so"

def _self_check():
    here = os.path.dirname(__file__)
    name = _lib_name()
    path = os.path.join(here, name)
    if not os.path.exists(path):
        raise ImportError(f"Native library {name} not found at: {path}")


if __name__ == "__main__":
    setup(
        distclass=BinaryDistribution,
        cmdclass={"bdist_wheel": bdist_wheel, "build_py": BuildTacozipExt},
    )