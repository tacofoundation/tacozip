import shutil
import sys
from pathlib import Path

from setuptools import setup, Distribution
from setuptools.command.build_py import build_py as _build_py
from wheel.bdist_wheel import bdist_wheel as _bdist_wheel


def _lib_names():
    if sys.platform == "win32":
        return ["tacozip.dll", "libtacozip.dll"]
    elif sys.platform == "darwin":
        return ["libtacozip.dylib"]
    else:
        return ["libtacozip.so"]


def _lib_patterns():
    if sys.platform == "win32":
        return ["tacozip*.dll", "libtacozip*.dll"]
    elif sys.platform == "darwin":
        return ["libtacozip*.dylib"]
    else:
        return ["libtacozip*.so*"]


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
        src_pkg = Path(self.get_package_dir("tacozip"))
        # build_py has already staged the package here; the library must land in
        # this tree, not in the source tree, to make it into the wheel.
        build_pkg = Path(self.build_lib) / "tacozip"
        build_pkg.mkdir(parents=True, exist_ok=True)

        for name in _lib_names():
            if (build_pkg / name).exists():
                print(f"setup.py: library already staged: {build_pkg / name}")
                return
            if (src_pkg / name).exists():
                print(f"setup.py: staging {src_pkg / name} -> {build_pkg / name}")
                shutil.copy2(src_pkg / name, build_pkg / name)
                return

        # Fallback for local `pip install .` against a cmake build tree.
        build_dir = Path(__file__).resolve().parents[2] / "build" / "release"
        for pattern in _lib_patterns():
            hits = sorted(build_dir.glob(f"**/{pattern}"))
            if hits:
                dest = build_pkg / _lib_names()[0]
                print(f"setup.py: staging {hits[0]} -> {dest}")
                shutil.copy2(hits[0], dest)
                return

        raise SystemExit(
            f"setup.py: no native library found. Looked for {_lib_names()} in "
            f"{src_pkg} and {_lib_patterns()} under {build_dir}. "
            f"Run prebuild.py to build and stage it before packaging."
        )


if __name__ == "__main__":
    setup(
        distclass=BinaryDistribution,
        cmdclass={"bdist_wheel": bdist_wheel, "build_py": BuildTacozipExt},
    )
