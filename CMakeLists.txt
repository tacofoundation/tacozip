cmake_minimum_required(VERSION 3.15)

project(tacozip
  VERSION 0.13.0
  LANGUAGES C
  DESCRIPTION "The ZIP container for TACO files"
)

add_compile_definitions(TACOZIP_VERSION_STRING="${PROJECT_VERSION}")

if(WIN32)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_SHARED_LIBS              "Build shared libraries" ON)
option(TACOZIP_BUILD_STATIC           "Also build a static lib" OFF)
option(TACOZIP_DEBUG_BUILD            "Enable debug build with TACOZIP_DEBUG statements" OFF)
option(TACOZIP_STRICT_WARNINGS        "Enable strict warnings" ON)
option(TACOZIP_WARNINGS_AS_ERRORS     "Treat warnings as errors" ON)
option(TACOZIP_ENABLE_IPO             "Enable LTO/IPO when supported" ON)
option(TACOZIP_ENABLE_SANITIZERS      "Enable sanitizers (Debug-only, GCC/Clang)" OFF)
option(TACOZIP_SET_UTF8_FLAG          "Set UTF-8 general purpose bit (compile-time)" OFF)

set(TACO_HEADER_MAX_ENTRIES 7 CACHE STRING "Maximum metadata entries in header (hardcoded)")
set(TACO_HEADER_PAYLOAD_SIZE 116 CACHE STRING "Size of TACO header payload in bytes (hardcoded)")

include(GNUInstallDirs)

# Find ZLIB
if(WIN32)
  set(ZLIB_ROOT "${CMAKE_PREFIX_PATH}")
endif()
find_package(ZLIB REQUIRED)

# Find libzip
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBZIP libzip>=1.0.0)
endif()

if(NOT LIBZIP_FOUND)
  if(WIN32)
    set(CMAKE_FIND_ROOT_PATH "${CMAKE_PREFIX_PATH}")
  endif()
  
  find_path(LIBZIP_INCLUDE_DIRS 
    NAMES zip.h
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
  )
  find_library(LIBZIP_LIBRARIES 
    NAMES zip libzip
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
    NO_DEFAULT_PATH
  )
  
  if(NOT LIBZIP_INCLUDE_DIRS OR NOT LIBZIP_LIBRARIES)
    find_path(LIBZIP_INCLUDE_DIRS zip.h)
    find_library(LIBZIP_LIBRARIES NAMES zip libzip)
  endif()
  
  if(NOT LIBZIP_INCLUDE_DIRS OR NOT LIBZIP_LIBRARIES)
    message(FATAL_ERROR "libzip not found. Please install libzip development packages.")
  endif()
  set(LIBZIP_CFLAGS "")
  set(LIBZIP_LDFLAGS "")
  set(LIBZIP_FOUND TRUE)
endif()

# Build library
set(TACOZIP_SOURCES src/tacozip.c)

add_library(tacozip ${TACOZIP_SOURCES})

if(TACOZIP_BUILD_STATIC)
  add_library(tacozip_static STATIC ${TACOZIP_SOURCES})
  set_target_properties(tacozip_static PROPERTIES OUTPUT_NAME tacozip)
endif()

target_include_directories(tacozip
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${LIBZIP_INCLUDE_DIRS}
)
if(TACOZIP_BUILD_STATIC)
  target_include_directories(tacozip_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${LIBZIP_INCLUDE_DIRS}
  )
endif()

target_compile_features(tacozip PUBLIC c_std_11)
if(TACOZIP_BUILD_STATIC)
  target_compile_features(tacozip_static PUBLIC c_std_11)
endif()

target_link_libraries(tacozip 
  PRIVATE 
    ${LIBZIP_LIBRARIES}
    ZLIB::ZLIB
)
if(TACOZIP_BUILD_STATIC)
  target_link_libraries(tacozip_static 
    PRIVATE 
      ${LIBZIP_LIBRARIES}
      ZLIB::ZLIB
  )
endif()

foreach(t IN ITEMS tacozip tacozip_static)
  if(TARGET ${t})
    target_compile_definitions(${t}
      PRIVATE
        TACOZIP_BUILD=1
        _FILE_OFFSET_BITS=64
        _GNU_SOURCE
        $<$<BOOL:${TACOZIP_SET_UTF8_FLAG}>:TACOZ_SET_UTF8_FLAG=1>
        $<$<BOOL:${TACOZIP_DEBUG_BUILD}>:TACOZIP_DEBUG_BUILD=1>
    )
    target_compile_options(${t} PRIVATE ${LIBZIP_CFLAGS})
    target_link_options(${t} PRIVATE ${LIBZIP_LDFLAGS})
  endif()
endforeach()

if(NOT MSVC)
  target_compile_options(tacozip PRIVATE -fvisibility=hidden)
  if(TACOZIP_BUILD_STATIC)
    target_compile_options(tacozip_static PRIVATE -fvisibility=hidden)
  endif()
endif()

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
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
          target_compile_options(${t} PRIVATE -Werror -Wno-error=nullability-extension)
        else()
          target_compile_options(${t} PRIVATE -Werror)
        endif()
      endif()
    endif()
  endif()
endforeach()

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-nullability-extension)
endif()

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

# Release optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(t IN ITEMS tacozip tacozip_static)
      if(TARGET ${t})
        target_compile_options(${t} PRIVATE
          -O3
          -ffast-math
          -fomit-frame-pointer
        )
        target_link_options(${t} PRIVATE
          $<$<PLATFORM_ID:Linux>:-Wl,--strip-all>
          $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        )
      endif()
    endforeach()
  elseif(MSVC)
    foreach(t IN ITEMS tacozip tacozip_static)
      if(TARGET ${t})
        target_compile_options(${t} PRIVATE /O2 /GL)
        target_link_options(${t} PRIVATE /LTCG)
      endif()
    endforeach()
  endif()
endif()

set_target_properties(tacozip PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  SOVERSION ${PROJECT_VERSION_MAJOR}
  VERSION   ${PROJECT_VERSION}
  OUTPUT_NAME tacozip
)
if(TACOZIP_BUILD_STATIC)
  set_target_properties(tacozip_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Installation
if(NOT DEFINED SKBUILD)
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
  install(TARGETS tacozip
    RUNTIME DESTINATION tacozip
    LIBRARY DESTINATION tacozip
    ARCHIVE DESTINATION tacozip
  )
  
  if(WIN32 AND CMAKE_PREFIX_PATH)
    foreach(dep_name IN ITEMS "zlib1" "zip")
      find_file(${dep_name}_DLL 
        NAMES "${dep_name}.dll"
        PATHS ${CMAKE_PREFIX_PATH}
        PATH_SUFFIXES bin
        NO_DEFAULT_PATH
      )
      if(${dep_name}_DLL)
        install(FILES ${${dep_name}_DLL} DESTINATION tacozip)
      endif()
    endforeach()
  endif()
endif()