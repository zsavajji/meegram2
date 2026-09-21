cmake_minimum_required(VERSION 3.10)

# quirc, the QR decoder behind QrScanner. Four C files and two headers with no dependencies
# of their own, so it is compiled here rather than cross-built by
# tools/setup-dependencies.sh: that script is for libraries with their own build systems and
# their own install steps, and this has neither.
#
# The counterpart to lib/QR-Code-generator, which encodes. Decoding is a different problem
# and a different library; neither does the other's half.
set(QUIRC_SOURCES
    ${CMAKE_SOURCE_DIR}/lib/quirc/lib/decode.c
    ${CMAKE_SOURCE_DIR}/lib/quirc/lib/identify.c
    ${CMAKE_SOURCE_DIR}/lib/quirc/lib/quirc.c
    ${CMAKE_SOURCE_DIR}/lib/quirc/lib/version_db.c
)

add_library(Quirc STATIC ${QUIRC_SOURCES})

target_include_directories(Quirc PUBLIC ${CMAKE_SOURCE_DIR}/lib/quirc/lib)

set_target_properties(Quirc PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    # meegram links -pie, and a non-PIC static archive will not link into a PIE executable
    # on ARM. Same reason rlottie and libwebp are built with it.
    POSITION_INDEPENDENT_CODE ON
)

# Not ours to fix, and this project builds with -Werror in Debug.
target_compile_options(Quirc PRIVATE -w)

add_library(Lib::Quirc ALIAS Quirc)
