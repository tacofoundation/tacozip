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
        
        print(f"setup.py: Package directory is: {pkg_dir}")

        # Check if library already exists in package dir
        if sys.platform == "win32":
            lib_names = ["tacozip.dll", "libtacozip.dll"]
        elif sys.platform == "darwin":
            lib_names = ["libtacozip.dylib"]
        else:
            lib_names = ["libtacozip.so"]

        # First check if library is already in the package directory
        for lib_name in lib_names:
            if (pkg_dir / lib_name).exists():
                print(f"setup.py: Found library already in package: {pkg_dir / lib_name}")
                return

        print("setup.py: Library not found in package, attempting to build or find...")
        
        # Find project root - go up from clients/python to find CMakeLists.txt
        search_roots = [
            Path(__file__).resolve().parents[2],  # tacozip root (clients/python -> clients -> root)
            Path(__file__).resolve().parents[1],  # clients directory
            Path(__file__).resolve().parent,      # python directory
            Path.cwd(),
        ]
        
        project_root = None
        for root in search_roots:
            if (root / "CMakeLists.txt").exists():
                project_root = root
                print(f"setup.py: Found project root at: {project_root}")
                break
        
        # Try to copy from existing build first
        if project_root:
            build_dir = project_root / "build" / "release"
            if build_dir.exists():
                print(f"setup.py: Found existing build directory: {build_dir}")
                if self._try_copy_from_build_dir(build_dir, pkg_dir):
                    return
        
        # Try to build the library ourselves if nothing found
        if project_root:
            try:
                print("setup.py: Attempting to build library ourselves...")
                build_dir = project_root / "build" / "release"
                print(f"setup.py: Building at: {build_dir}")
                
                # Create and run cmake commands
                import subprocess
                
                # Configure
                result = subprocess.run([
                    "cmake", "-S", str(project_root), "-B", str(build_dir),
                    "-DCMAKE_BUILD_TYPE=Release", "-DTACOZIP_ENABLE_IPO=OFF"
                ], capture_output=True, text=True)
                print(f"setup.py: cmake configure result: {result.returncode}")
                if result.stdout: print(f"setup.py: cmake stdout: {result.stdout}")
                if result.stderr: print(f"setup.py: cmake stderr: {result.stderr}")
                
                if result.returncode == 0:
                    # Build
                    result = subprocess.run([
                        "cmake", "--build", str(build_dir), "-j"
                    ], capture_output=True, text=True)
                    print(f"setup.py: cmake build result: {result.returncode}")
                    if result.stdout: print(f"setup.py: cmake build stdout: {result.stdout}")
                    if result.stderr: print(f"setup.py: cmake build stderr: {result.stderr}")
                    
                    if result.returncode == 0:
                        print(f"setup.py: Build succeeded, copying from: {build_dir}")
                        if self._try_copy_from_build_dir(build_dir, pkg_dir):
                            return
                
            except (subprocess.CalledProcessError, FileNotFoundError) as e:
                print(f"setup.py: Failed to build library: {e}")

        # Search for existing build directory in various locations
        search_paths = [
            Path.cwd() / "build" / "release",
            Path(__file__).resolve().parent / "build" / "release", 
            Path(__file__).resolve().parents[1] / "build" / "release",
            Path(__file__).resolve().parents[2] / "build" / "release",
            Path(__file__).resolve().parents[3] / "build" / "release",
        ]
        
        print("setup.py: Searching for build directories:")
        for path in search_paths:
            print(f"setup.py:   - {path} (exists: {path.exists()})")
            if path.exists() and self._try_copy_from_build_dir(path, pkg_dir):
                return
        
        # Create a dummy library file as last resort
        print("setup.py: WARNING: Creating dummy library file - this wheel will not work!")
        if sys.platform == "win32":
            dummy_name = "tacozip.dll"
        elif sys.platform == "darwin":
            dummy_name = "libtacozip.dylib"
        else:
            dummy_name = "libtacozip.so"
        
        dummy_path = pkg_dir / dummy_name
        dummy_path.write_text("# Dummy library - build failed")
        print(f"setup.py: Created dummy file: {dummy_path}")

    def _try_copy_from_build_dir(self, build_dir, pkg_dir):
        """Try to copy library from a build directory. Returns True if successful."""
        # Platform-specific library search patterns
        if sys.platform == "win32":
            patterns = ["tacozip*.dll", "libtacozip*.dll"]
            dest_name = "tacozip.dll"
        elif sys.platform == "darwin":
            patterns = ["libtacozip*.dylib"]
            dest_name = "libtacozip.dylib"
        else:
            patterns = ["libtacozip*.so*"]
            dest_name = "libtacozip.so"

        print(f"setup.py: Looking in {build_dir} for patterns: {patterns}")
        
        for pat in patterns:
            cand = list(build_dir.glob(f"**/{pat}"))
            print(f"setup.py: Pattern {pat} found: {[str(c) for c in cand]}")
            if cand:
                src = cand[0]
                dest = pkg_dir / dest_name
                print(f"setup.py: Copying library: {src} -> {dest}")
                shutil.copy2(src, dest)
                return True
        
        return False

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
    path = os.path.join(here, "tacozip", name)
    if not os.path.exists(path):
        raise ImportError(f"Native library {name} not found at: {path}")

if __name__ == "__main__":
    setup(
        distclass=BinaryDistribution,
        cmdclass={"bdist_wheel": bdist_wheel, "build_py": BuildTacozipExt},
    )