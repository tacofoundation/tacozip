import os
import sys
import subprocess
import shutil
from pathlib import Path
from setuptools import setup, Distribution
from setuptools.command.build_ext import build_ext
from wheel.bdist_wheel import bdist_wheel as _bdist_wheel

class BinaryDistribution(Distribution):
    def has_ext_modules(self):  # tells setuptools this is a binary wheel
        return True

class bdist_wheel(_bdist_wheel):
    def finalize_options(self):
        super().finalize_options()
        self.root_is_pure = False  # put files under platlib, not purelib

class BuildTacozipExt(build_ext):
    """Custom build_ext that compiles tacozip C library if not present."""
    
    def run(self):
        self.build_tacozip_if_needed()
        super().run()
    
    def build_tacozip_if_needed(self):
        """Build tacozip shared library if not already present."""
        pkg_dir = Path(__file__).parent / "tacozip"
        
        # Check if shared library already exists (from wheel)
        lib_patterns = ["libtacozip.so", "libtacozip.dylib", "tacozip.dll", "libtacozip.dll"]
        if any((pkg_dir / pattern).exists() for pattern in lib_patterns):
            print("Found existing tacozip shared library, skipping build")
            return
        
        print("No prebuilt tacozip library found, building from source...")
        
        # Find project root (should contain CMakeLists.txt)
        project_root = self.find_project_root()
        if not project_root:
            raise RuntimeError("Could not find project root with CMakeLists.txt")
        
        # Build the library
        self.compile_tacozip(project_root, pkg_dir)
    
    def find_project_root(self):
        """Find the project root by looking for CMakeLists.txt."""
        current = Path(__file__).parent
        
        # Look in current directory and up to 3 levels up
        for _ in range(4):
            if (current / "CMakeLists.txt").exists():
                return current
            parent = current.parent
            if parent == current:  # reached filesystem root
                break
            current = parent
        
        return None
    
    def compile_tacozip(self, project_root, pkg_dir):
        """Compile tacozip using CMake."""
        build_dir = project_root / "build" / "python-fallback"
        
        try:
            # Ensure build directory exists
            build_dir.mkdir(parents=True, exist_ok=True)
            
            # Check for cmake
            cmake_cmd = shutil.which("cmake")
            if not cmake_cmd:
                raise RuntimeError(
                    "cmake not found. Please install cmake to build from source:\n"
                    "  pip install cmake\n"
                    "or install via your system package manager"
                )
            
            print(f"Building tacozip in {build_dir}")
            
            # Configure
            configure_cmd = [
                cmake_cmd, str(project_root),
                f"-B{build_dir}",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DTACOZIP_ENABLE_IPO=OFF",  # Disable LTO for compatibility
                "-DBUILD_SHARED_LIBS=ON"
            ]
            
            # Add platform-specific flags
            if sys.platform == "darwin":
                configure_cmd.extend([
                    "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15"
                ])
            
            print(f"Running: {' '.join(configure_cmd)}")
            subprocess.run(configure_cmd, check=True, cwd=project_root)
            
            # Build
            build_cmd = [cmake_cmd, "--build", str(build_dir), "-j"]
            print(f"Running: {' '.join(build_cmd)}")
            subprocess.run(build_cmd, check=True, cwd=project_root)
            
            # Copy the built library to package directory
            self.copy_built_library(build_dir, pkg_dir)
            
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"Failed to build tacozip: {e}")
        except Exception as e:
            raise RuntimeError(f"Build error: {e}")
    
    def copy_built_library(self, build_dir, pkg_dir):
        """Copy the built shared library to the package directory."""
        pkg_dir.mkdir(parents=True, exist_ok=True)
        
        # Platform-specific library names and locations
        if sys.platform.startswith("linux"):
            lib_file = "libtacozip.so"
        elif sys.platform == "darwin":
            lib_file = "libtacozip.dylib"
        elif sys.platform == "win32":
            lib_file = "tacozip.dll"
        else:
            lib_file = "libtacozip.so"  # fallback
        
        # Find the built library
        built_lib = build_dir / lib_file
        if not built_lib.exists():
            # Try alternative locations/names
            alternatives = [
                build_dir / "Release" / lib_file,
                build_dir / "Debug" / lib_file,
            ]
            for alt in alternatives:
                if alt.exists():
                    built_lib = alt
                    break
            else:
                raise RuntimeError(f"Could not find built library {lib_file} in {build_dir}")
        
        target_lib = pkg_dir / lib_file
        print(f"Copying {built_lib} to {target_lib}")
        shutil.copy2(built_lib, target_lib)
        
        # Ensure the copied file is readable
        target_lib.chmod(0o755)

setup(
    distclass=BinaryDistribution,
    cmdclass={
        "bdist_wheel": bdist_wheel,
        "build_ext": BuildTacozipExt,
    },
    # Add cmake as a build dependency for source builds
    setup_requires=["cmake>=3.15"] if not os.getenv("CIBW_BUILD") else [],
)