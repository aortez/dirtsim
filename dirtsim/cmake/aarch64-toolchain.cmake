# CMake toolchain file for cross-compiling to Raspberry Pi (aarch64).
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake -B build-aarch64 .
#   make -C build-aarch64 -j$(nproc)
#
# Prerequisites:
#   1. Install cross-compiler: sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#   2. Sync sysroot from Pi: ./cmake/aarch64-sysroot-sync.sh

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compiler.
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Sysroot - contains headers and libraries from the Pi.
set(SYSROOT_DIR "${CMAKE_SOURCE_DIR}/sysroot-aarch64")

if(EXISTS "${SYSROOT_DIR}")
    set(CMAKE_SYSROOT "${SYSROOT_DIR}")
    set(CMAKE_FIND_ROOT_PATH "${SYSROOT_DIR}")

    # Tell pkg-config to look in the sysroot.
    set(ENV{PKG_CONFIG_PATH} "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu/pkgconfig:${SYSROOT_DIR}/usr/share/pkgconfig:${SYSROOT_DIR}/usr/lib/pkgconfig")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${SYSROOT_DIR}")
    set(ENV{PKG_CONFIG_LIBDIR} "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu/pkgconfig:${SYSROOT_DIR}/usr/share/pkgconfig")

    message(STATUS "Cross-compiling with sysroot: ${SYSROOT_DIR}")
else()
    message(WARNING "Sysroot not found at ${SYSROOT_DIR}")
    message(WARNING "Run ./cmake/aarch64-sysroot-sync.sh to create it")
endif()

# Search paths - where to find libraries and headers.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)    # Never find host programs.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)     # Only find target libraries.
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)     # Only find target headers.
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)     # Only find target packages.

# Linker flags to use the sysroot's dynamic linker.
if(EXISTS "${SYSROOT_DIR}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--sysroot=${SYSROOT_DIR}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,--sysroot=${SYSROOT_DIR}")
endif()

# Force static linking for FetchContent dependencies to avoid runtime issues.
# The FetchContent libs (libdatachannel, googletest, etc.) will be built
# for aarch64 and linked statically.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

# Compiler flags for better compatibility.
set(CMAKE_C_FLAGS_INIT "-march=armv8-a")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a")
