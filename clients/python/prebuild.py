#!/usr/bin/env python3
"""
Pre-build script to ensure the native library is available before wheel building.
This runs as part of CIBW_BEFORE_BUILD instead of CIBW_BEFORE_ALL.
"""

import sys
import subprocess
import shutil
from pathlib import Path
import os

def main():
    print("=== prebuild.py: Starting pre-build process ===")
    
    # Find project root
    script_dir = Path(__file__).parent
    project_root = script_dir
    
    # Look for CMakeLists.txt going up the directory tree
    for i in range(5):  # Go up max 5 levels
        if (project_root / "CMakeLists.txt").exists():
            print(f"prebuild.py: Found project root at: {project_root}")
            break
        project_root = project_root.parent
    else:
        print("prebuild.py: ERROR: Could not find CMakeLists.txt")
        return 1
    
    # Set up paths
    build_dir = project_root / "build" / "release"
    package_dir = script_dir / "tacozip"
    
    print(f"prebuild.py: Project root: {project_root}")
    print(f"prebuild.py: Build directory: {build_dir}")
    print(f"prebuild.py: Package directory: {package_dir}")
    
    # Ensure package directory exists
    package_dir.mkdir(parents=True, exist_ok=True)
    
    # Determine library names
    if sys.platform == "win32":
        lib_patterns = ["tacozip*.dll", "libtacozip*.dll"]
        dest_name = "tacozip.dll"
    elif sys.platform == "darwin":
        lib_patterns = ["libtacozip*.dylib"]
        dest_name = "libtacozip.dylib"
    else:
        lib_patterns = ["libtacozip*.so*"]
        dest_name = "libtacozip.so"
    
    # Check if library already exists
    dest_path = package_dir / dest_name
    if dest_path.exists():
        print(f"prebuild.py: Library already exists: {dest_path}")
        return 0
    
    # Try to find existing built library first
    if build_dir.exists():
        print(f"prebuild.py: Checking existing build directory: {build_dir}")
        for pattern in lib_patterns:
            matches = list(build_dir.glob(f"**/{pattern}"))
            if matches:
                src = matches[0]
                print(f"prebuild.py: Found existing library: {src}")
                print(f"prebuild.py: Copying to: {dest_path}")
                shutil.copy2(src, dest_path)
                return 0
    
    # Build the library
    print("prebuild.py: Building library...")
    
    try:
        # Clean and create build directory
        if build_dir.exists():
            shutil.rmtree(build_dir)
        build_dir.mkdir(parents=True, exist_ok=True)
        
        # Configure
        print("prebuild.py: Running cmake configure...")
        env = os.environ.copy()
        configure_cmd = [
            "cmake", "-S", str(project_root), "-B", str(build_dir),
            "-DCMAKE_BUILD_TYPE=Release", "-DTACOZIP_ENABLE_IPO=OFF"
        ]
        
        # Platform-specific configuration
        if sys.platform == "win32":
            if shutil.which("ninja"):
                configure_cmd.extend(["-G", "Ninja"])
        elif sys.platform == "darwin":
            # Handle macOS universal2 builds
            if shutil.which("ninja"):
                configure_cmd.extend(["-G", "Ninja"])
            macos_target = env.get("MACOSX_DEPLOYMENT_TARGET", "11.0")
            configure_cmd.extend([
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET={macos_target}",
                "-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64"
            ])
        else:
            # Linux
            if shutil.which("ninja"):
                configure_cmd.extend(["-G", "Ninja"])
        
        result = subprocess.run(configure_cmd, env=env, capture_output=True, text=True)
        print(f"prebuild.py: Configure result: {result.returncode}")
        if result.stdout:
            print(f"prebuild.py: Configure stdout:\n{result.stdout}")
        if result.stderr:
            print(f"prebuild.py: Configure stderr:\n{result.stderr}")
        
        if result.returncode != 0:
            print("prebuild.py: Configure failed")
            return 1
        
        # Build
        print("prebuild.py: Running cmake build...")
        build_cmd = ["cmake", "--build", str(build_dir), "-j"]
        result = subprocess.run(build_cmd, env=env, capture_output=True, text=True)
        print(f"prebuild.py: Build result: {result.returncode}")
        if result.stdout:
            print(f"prebuild.py: Build stdout:\n{result.stdout}")
        if result.stderr:
            print(f"prebuild.py: Build stderr:\n{result.stderr}")
        
        if result.returncode != 0:
            print("prebuild.py: Build failed")
            return 1
        
        # Find and copy the built library
        print(f"prebuild.py: Looking for built library in: {build_dir}")
        for pattern in lib_patterns:
            matches = list(build_dir.glob(f"**/{pattern}"))
            print(f"prebuild.py: Pattern {pattern} matches: {[str(m) for m in matches]}")
            if matches:
                src = matches[0]
                print(f"prebuild.py: Copying {src} to {dest_path}")
                shutil.copy2(src, dest_path)
                print(f"prebuild.py: Successfully copied library")
                return 0
        
        print("prebuild.py: ERROR: No library found after build")
        return 1
        
    except Exception as e:
        print(f"prebuild.py: Exception during build: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())