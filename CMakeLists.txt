cmake_minimum_required(VERSION 3.15)

# -----------------------------------------------------------------------------
# tacozip — CIP64 (always ZIP64), STORE-only writer with fixed 64B "ghost"
# Strongly inspired by libzip's streaming layout; specialized for CIP64.
#
# Quickstart:
#   cmake --preset release
#   cmake --build --preset release -j
#   cmake --install --preset release
# -----------------------------------------------------------------------------
project(tacozip
  VERSION 0.2.0
  LANGUAGES C
  DESCRIPTION "TACO ZIP: ZIP64 archive with fixed 64-byte ghost LFH"
)

# Export all symbols on Windows if no .def is provided (helps produce tacozip.dll)
if(WIN32)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ----------------------------- sensible defaults -----------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_SHARED_LIBS              "Build shared libraries" ON)
option(TACOZIP_BUILD_STATIC           "Also build a static lib" OFF)
option(TACOZIP_STRICT_WARNINGS        "Enable strict warnings" ON)
option(TACOZIP_WARNINGS_AS_ERRORS     "Treat warnings as errors" ON)
option(TACOZIP_ENABLE_IPO             "Enable LTO/IPO when supported" ON)
option(TACOZIP_ENABLE_SANITIZERS      "Enable sanitizers (Debug-only, GCC/Clang)" OFF)
option(TACOZIP_SET_UTF8_FLAG          "Set UTF-8 general purpose bit (compile-time)" OFF)

# Buffer tunables (compile-time constants used by the C code)
set(TACOZ_OUT_BUFSZ  4194304  CACHE STRING "Output buffer size (bytes), default 4 MiB")
set(TACOZ_COPY_BUFSZ 1048576  CACHE STRING "Copy buffer size (bytes), default 1 MiB")

include(GNUInstallDirs)
include(CheckSymbolExists)

# ------------------------------- feature probes ------------------------------
# Cheap preallocation; exposed via config header for consumers.
check_symbol_exists(posix_fallocate "fcntl.h" TACOZ_HAVE_POSIX_FALLOCATE)

# Generated config header with feature toggles + buffer sizes.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tacozip_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/tacozip_config.h
  @ONLY
)

# --------------------------------- library -----------------------------------
set(TACOZIP_SOURCES
  src/tacozip.c
)

# Shared or static according to BUILD_SHARED_LIBS (default: shared).
add_library(tacozip ${TACOZIP_SOURCES})

# Optionally also build an explicit static variant for packagers.
if(TACOZIP_BUILD_STATIC)
  add_library(tacozip_static STATIC ${TACOZIP_SOURCES})
  set_target_properties(tacozip_static PROPERTIES OUTPUT_NAME tacozip)
endif()

# Public headers (project + generated config)
target_include_directories(tacozip
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>
)
if(TACOZIP_BUILD_STATIC)
  target_include_directories(tacozip_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>
  )
endif()

target_compile_features(tacozip PUBLIC c_std_11)
if(TACOZIP_BUILD_STATIC)
  target_compile_features(tacozip_static PUBLIC c_std_11)
endif()

# Large-file + GNU ext guards; UTF-8 flag + tunables
foreach(t IN ITEMS tacozip tacozip_static)
  if(TARGET ${t})
    target_compile_definitions(${t}
      PRIVATE
        TACOZIP_BUILD=1
        _FILE_OFFSET_BITS=64
        _GNU_SOURCE
        $<$<BOOL:${TACOZIP_SET_UTF8_FLAG}>:TACOZ_SET_UTF8_FLAG=1>
        TACOZ_OUT_BUFSZ=${TACOZ_OUT_BUFSZ}
        TACOZ_COPY_BUFSZ=${TACOZ_COPY_BUFSZ}
    )
  endif()
endforeach()

# Visibility (clean ABI on ELF)
if(NOT MSVC)
  target_compile_options(tacozip PRIVATE -fvisibility=hidden)
  if(TACOZIP_BUILD_STATIC)
    target_compile_options(tacozip_static PRIVATE -fvisibility=hidden)
  endif()
endif()

# Warnings (+ optional as errors)
foreach(t IN ITEMS tacozip tacozip_static)
  if(TARGET ${t})
    if(TACOZIP_STRICT_WARNINGS)
      if(MSVC)
        target_compile_options(${t} PRIVATE /W4)
      else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
      endif()
    endif()
    if(TACOZIP_WARNINGS_AS_ERRORS)
      if(MSVC)
        target_compile_options(${t} PRIVATE /WX)
      else()
        target_compile_options(${t} PRIVATE -Werror)
      endif()
    endif()
  endif()
endforeach()

# IPO/LTO for Release/RelWithDebInfo if supported
if(TACOZIP_ENABLE_IPO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
  if(ipo_ok)
    set_property(TARGET tacozip PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET tacozip PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    if(TACOZIP_BUILD_STATIC)
      set_property(TARGET tacozip_static PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
      set_property(TARGET tacozip_static PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    endif()
  endif()
endif()

# Sanitizers (opt-in; Debug/RelWithDebInfo, GCC/Clang)
if(TACOZIP_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
    add_link_options(-fsanitize=address,undefined)
    foreach(t IN ITEMS tacozip tacozip_static)
      if(TARGET ${t})
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${t}    PRIVATE -fsanitize=address,undefined)
      endif()
    endforeach()
  endif()
endif()

# Common properties
set_target_properties(tacozip PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  SOVERSION ${PROJECT_VERSION_MAJOR}
  VERSION   ${PROJECT_VERSION}
  OUTPUT_NAME tacozip
)
if(TACOZIP_BUILD_STATIC)
  set_target_properties(tacozip_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# --------------------------------- install -----------------------------------
# Split install logic: system-wide install vs. wheel (scikit-build) install.
# This avoids double-installing the same target when SKBUILD is defined.

if(NOT DEFINED SKBUILD)
  # ---- System-wide installation (headers, libs, CMake package, pkg-config) ----
  install(TARGETS tacozip
    EXPORT tacozip-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
  if(TACOZIP_BUILD_STATIC)
    install(TARGETS tacozip_static
      EXPORT tacozip-targets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
  endif()

  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
          FILES_MATCHING PATTERN "tacozip.h")
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/generated/tacozip_config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  # CMake package for find_package(tacozip)
  install(EXPORT tacozip-targets
    NAMESPACE tacozip::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tacozip
  )

  include(CMakePackageConfigHelpers)
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tacozipConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/tacozipConfig.cmake"
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/tacozip-targets.cmake\")\n")
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tacozipConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tacozipConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tacozip
  )

  # pkg-config (.pc) for non-CMake consumers
  set(prefix ${CMAKE_INSTALL_PREFIX})
  set(exec_prefix "\${prefix}")
  set(libdir "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
  set(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tacozip.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/tacozip.pc
    @ONLY
  )
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/tacozip.pc
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

else()
  # ---- Wheel build via scikit-build: install binary into the Python package ----
  # NOTE: Do not prefix with "python/". The wheel root will contain "tacozip/" already.
  install(TARGETS tacozip
    RUNTIME DESTINATION tacozip   # Windows (.dll)
    LIBRARY DESTINATION tacozip   # Linux/macOS (.so/.dylib)
    ARCHIVE DESTINATION tacozip
  )
endif()

# Build summary (helps users confirm toggles)
message(STATUS "==== tacozip build summary ====")
message(STATUS "Version                : ${PROJECT_VERSION}")
message(STATUS "Build type             : ${CMAKE_BUILD_TYPE}")
message(STATUS "Shared lib             : ${BUILD_SHARED_LIBS}")
message(STATUS "Static lib (extra)     : ${TACOZIP_BUILD_STATIC}")
message(STATUS "UTF-8 flag default     : ${TACOZIP_SET_UTF8_FLAG}")
message(STATUS "Out buffer (bytes)     : ${TACOZ_OUT_BUFSZ}")
message(STATUS "Copy buffer (bytes)    : ${TACOZ_COPY_BUFSZ}")
message(STATUS "IPO/LTO                : ${TACOZIP_ENABLE_IPO}")
message(STATUS "Sanitizers             : ${TACOZIP_ENABLE_SANITIZERS}")
message(STATUS "posix_fallocate()      : ${TACOZ_HAVE_POSIX_FALLOCATE}")
message(STATUS "Install prefix         : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "SKBUILD defined        : $<IF:$<BOOL:${SKBUILD}>,YES,NO>")
message(STATUS "================================")
