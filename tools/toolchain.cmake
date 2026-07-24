# Cross-compilation toolchain for MeeGo 1.2 Harmattan (Nokia N9).
#
#   cmake -B build \
#     -DCMAKE_TOOLCHAIN_FILE=tools/toolchain.cmake \
#     -DQT_SDK_PATH=/path/to/QtSDK \
#     -DBUILD_HARMATTAN=ON
#
# QT_SDK_PATH and TOOLCHAIN_PREFIX may also come from the environment. The compiler
# itself is built by tools/build-toolchain.sh - the SDK's own GCC 4.4.1 cannot
# compile this project.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED TOOLCHAIN_PREFIX)
    if(DEFINED ENV{TOOLCHAIN_PREFIX})
        set(TOOLCHAIN_PREFIX "$ENV{TOOLCHAIN_PREFIX}")
    else()
        set(TOOLCHAIN_PREFIX "arm-none-linux-gnueabi")
    endif()
endif()

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}-gcc)

# The gcc-* wrappers, not plain binutils: they pass liblto_plugin.so, without which
# ar/ranlib/nm cannot read the GIMPLE objects that LTO produces.
set(CMAKE_AR      ${TOOLCHAIN_PREFIX}-gcc-ar     CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB  ${TOOLCHAIN_PREFIX}-gcc-ranlib CACHE FILEPATH "" FORCE)
set(CMAKE_NM      ${TOOLCHAIN_PREFIX}-gcc-nm     CACHE FILEPATH "" FORCE)

if(NOT DEFINED QT_SDK_PATH AND DEFINED ENV{QT_SDK_PATH})
    set(QT_SDK_PATH "$ENV{QT_SDK_PATH}")
endif()

if(QT_SDK_PATH)
    set(HARMATTAN_SYSROOT "${QT_SDK_PATH}/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim")

    if(EXISTS "${HARMATTAN_SYSROOT}")
        set(CMAKE_SYSROOT "${HARMATTAN_SYSROOT}")

        # Without this, find_package() only searches the host and fails with
        # "Could not find a package configuration file provided by Td".
        list(APPEND CMAKE_FIND_ROOT_PATH "${HARMATTAN_SYSROOT}")

        # tools/setup-dependencies.sh installs TDLib and rlottie under /usr/local
        # inside the sysroot; Qt and the rest of the platform live under /usr.
        list(APPEND CMAKE_PREFIX_PATH
            "${HARMATTAN_SYSROOT}/usr/local"
            "${HARMATTAN_SYSROOT}/usr")
    else()
        message(WARNING "Harmattan sysroot not found at ${HARMATTAN_SYSROOT} - "
                        "find_package() will search the host instead and pick up the wrong Qt.")
    endif()
else()
    message(WARNING "QT_SDK_PATH is not set, so no sysroot is configured. "
                    "Pass -DQT_SDK_PATH=/path/to/QtSDK.")
endif()

# Programs are host binaries (moc, rcc, the compiler itself); headers, libraries and
# CMake packages must come from the sysroot. Anything in the sysroot's bin/ is an ARM
# binary and cannot run here, which is why PROGRAM is NEVER rather than ONLY.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
