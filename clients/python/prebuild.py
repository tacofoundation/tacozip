#!/usr/bin/env python3
"""
Pre-build script to ensure the native library is available before wheel building.
Updated for new modular package structure and better Windows path handling.
"""

import sys
import subprocess
import shutil
from pathlib import Path
import os

def fix_newlines_for_macos():
    """Fix missing newlines at end of files for Apple Clang compatibility."""
    print("prebuild.py: Checking newlines for Apple Clang...")
    
    # Find project root (2 levels up from clients/python/)
    current = Path(__file__).parent.parent.parent
    
    files_to_fix = [
        current / "include" / "tacozip.h",
        current / "src" / "tacozip.c", 
        current / "include" / "tacozip_config.h.in"
    ]
    
    fixed_count = 0
    for file_path in files_to_fix:
        if file_path.exists():
            try:
                with open(file_path, 'rb') as f:
                    content = f.read()
                
                if content and not content.endswith(b'\n'):
                    with open(file_path, 'ab') as f:
                        f.write(b'\n')
                    print(f"prebuild.py: Fixed {file_path.name}")
                    fixed_count += 1
                else:
                    print(f"prebuild.py: {file_path.name} already OK")
            except Exception as e:
                print(f"prebuild.py: Error with {file_path}: {e}")
        else:
            print(f"prebuild.py: Warning - {file_path} not found")
    
    print(f"prebuild.py: Fixed {fixed_count} files")

def copy_windows_dependencies(package_dir):
    """Copy Windows DLL dependencies to the package directory."""
    if sys.platform != "win32":
        return
    
    print("prebuild.py: Copying Windows DLL dependencies...")
    deps_path = Path("C:/deps/bin")
    
    if not deps_path.exists():
        print(f"prebuild.py: Warning - Dependencies path not found: {deps_path}")
        return
    
    dlls_to_copy = ["zlib1.dll", "zip.dll"]
    copied_count = 0
    
    for dll_name in dlls_to_copy:
        src_dll = deps_path / dll_name
        if src_dll.exists():
            dest_dll = package_dir / dll_name
            shutil.copy2(src_dll, dest_dll)
            print(f"prebuild.py: Copied {dll_name}")
            copied_count += 1
        else:
            print(f"prebuild.py: Warning - {dll_name} not found at {src_dll}")
    
    print(f"prebuild.py: Copied {copied_count} Windows DLLs")

def main():
    print("=== prebuild.py: Starting pre-build process ===")
    
    # Fix newlines first (critical for macOS)
    fix_newlines_for_macos()
    
    # Find project root (where CMakeLists.txt should be)
    # Since we're in clients/python/, go up 2 levels
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent
    
    if not (project_root / "CMakeLists.txt").exists():
        # Try alternative paths
        for i in range(5):
            candidate = script_dir
            for _ in range(i):
                candidate = candidate.parent
            if (candidate / "CMakeLists.txt").exists():
                project_root = candidate
                break
        else:
            print("prebuild.py: ERROR: Could not find CMakeLists.txt")
            return 1
    
    print(f"prebuild.py: Found project root at: {project_root}")
    
    # Set up paths
    build_dir = project_root / "build" / "release"
    package_dir = script_dir / "tacozip"  # Main package directory
    
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
            # Add Windows dependency paths if they exist
            deps_path = "C:/deps"
            if Path(deps_path).exists():
                configure_cmd.append(f"-DCMAKE_PREFIX_PATH={deps_path}")
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
                
                # Copy Windows dependencies after successful build
                copy_windows_dependencies(package_dir)
                
                return 0
        
        print("prebuild.py: ERROR: No library found after build")
        return 1
        
    except Exception as e:
        print(f"prebuild.py: Exception during build: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())