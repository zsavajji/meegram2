#!/usr/bin/env bash

set -euo pipefail

# Define colors for output
declare -r COLOR_RESET="\033[0m"
declare -r COLOR_RED="\033[31m"
declare -r COLOR_GREEN="\033[32m"
declare -r COLOR_BLUE="\033[34m"
declare -r COLOR_CYAN="\033[36m"
declare -r COLOR_MAGENTA="\033[35m"

# Arguments and paths
readonly ARGS="${1:-}"
readonly SDK_PATH="${2:-}"

# Pinned TDLib revision. TDLib tags almost nothing (its git tags stop at v1.8.0
# while it reports versions like 1.8.x), so a commit is the only way to pin it.
# Bump deliberately, after verifying a build actually runs on the device.
readonly TDLIB_COMMIT="${TDLIB_COMMIT:-022d60202e446ad1287b9fb68e687c8a0760788b}"

# Pinned rlottie revision. Its newest tag is v0.2 from 2020, well behind master, so
# like TDLib this is pinned by commit rather than by tag.
readonly RLOTTIE_COMMIT="${RLOTTIE_COMMIT:-2cab35db755b0e39df40b679969495e90d39c578}"

# libwebp, for static stickers. Qt 4.7 has no WebP handler and the device ships none,
# so without this every static sticker falls back to its emoji. v1.4.0 is a release
# tag, unlike rlottie, so pin to that.
readonly LIBWEBP_TAG="${LIBWEBP_TAG:-v1.4.0}"

# libogg and libopus, for voice notes. TDLib accepts nothing but OggOpus for them, and
# neither the codec nor a GStreamer new enough to have gained it is in the Harmattan
# image - opus was standardised the year after this device shipped. Both are release
# tags and both build with CMake, which is why they go through the same path as libwebp
# rather than needing autotools cross-configuration. See src/OggOpus.cpp.
readonly LIBOGG_TAG="${LIBOGG_TAG:-v1.3.5}"
readonly OPUS_TAG="${OPUS_TAG:-v1.4}"

# Parallel build jobs.
#
# TDLib used to build fully serial here: `cmake --build` defaults to one job with
# the Makefile generator, while zlib - which takes seconds - got -j4. This applies
# the same job count everywhere.
#
# Do not raise this blindly. TDLib's generated td_api translation units are
# memory-hungry (a gigabyte or more each is normal) and TD_ENABLE_LTO adds
# link-time pressure on top, so parallelism costs RAM more than it costs CPU.
# 4 is comfortable on 8 GB. Drop to JOBS=2 or JOBS=1 if the build is OOM-killed.
readonly JOBS="${JOBS:-4}"

# Link-time optimisation for TDLib. Worth having on a size-constrained device, but
# it requires the gcc-ar/gcc-ranlib/gcc-nm wrappers (checked in check_harmattan_env).
# Set TDLIB_LTO=OFF to build without it if your toolchain lacks them.
readonly TDLIB_LTO="${TDLIB_LTO:-ON}"

# Validate everything the Harmattan path needs up front. All of these used to fail
# late - the SDK path in particular is only read at the very end, after TDLib has
# finished building, so a missing one wasted the whole compile.
check_harmattan_env() {
    if [[ "$ARGS" != "harmattan" ]]; then
        return
    fi

    if [[ -z "${TOOLCHAIN_PREFIX:-}" ]]; then
        error "TOOLCHAIN_PREFIX is not set. Please define it before running the script."
        error "  export TOOLCHAIN_PREFIX=arm-none-linux-gnueabi"
        exit 1
    fi

    if ! command -v "$TOOLCHAIN_PREFIX-gcc" >/dev/null 2>&1; then
        error "$TOOLCHAIN_PREFIX-gcc is not on PATH."
        error "Build it with tools/build-toolchain.sh, then add its bin/ directory to PATH."
        exit 1
    fi

    # TDLib is built with LTO, which needs the gcc-* wrappers rather than plain
    # binutils - they pass liblto_plugin.so so ar/ranlib/nm can read GIMPLE objects.
    if [[ "$TDLIB_LTO" == "ON" ]]; then
        for tool in gcc-ar gcc-ranlib gcc-nm; do
            if ! command -v "$TOOLCHAIN_PREFIX-$tool" >/dev/null 2>&1; then
                error "$TOOLCHAIN_PREFIX-$tool is not on PATH, but TDLib is built with LTO."
                error "Without it you get: 'plugin needed to handle lto object'."
                error "Either install the wrapper, or build without LTO: TDLIB_LTO=OFF $0 ..."
                exit 1
            fi
        done
    fi

    if [[ -z "$SDK_PATH" ]]; then
        error "No SDK path given. Usage: $0 harmattan /path/to/QtSDK"
        error "Without it TDLib would be installed into /Madde/... at the filesystem root."
        exit 1
    fi

    local sysroot="$SDK_PATH/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim"

    if [[ ! -d "$sysroot" ]]; then
        error "Harmattan sysroot not found: $sysroot"
        error "Check that SDK_PATH points at the Qt SDK root (the directory holding Madde/)."
        exit 1
    fi

    info "Toolchain: $(command -v "$TOOLCHAIN_PREFIX-gcc")"
    info "Sysroot:   $sysroot"
}

# Functions for colored output
info() {
    echo -e "${COLOR_BLUE}INFO: $*${COLOR_RESET}"
}

success() {
    echo -e "${COLOR_GREEN}SUCCESS: $*${COLOR_RESET}"
}

warn() {
    echo -e "${COLOR_MAGENTA}WARNING: $*${COLOR_RESET}"
}

error() {
    echo -e "${COLOR_RED}ERROR: $*${COLOR_RESET}" >&2
}

# Validate arguments
validate_args() {
    if [ -z "$ARGS" ]; then
        error "No device specified. Please specify 'harmattan' or 'simulator'."
        exit 1
    fi

    if [[ "$ARGS" != "harmattan" && "$ARGS" != "simulator" ]]; then
        error "Incorrect device specified. Please specify 'harmattan' or 'simulator'."
        exit 1
    fi
}

# Writes the CMake toolchain file used by every cross-built CMake dependency.
#
# ar/ranlib/nm are the gcc-* wrappers, not plain binutils: with LTO the compiler
# emits GIMPLE bytecode into the .o files and plain ar cannot read it - BFD reports
# "plugin needed to handle lto object" and then either silently drops LTO or leaves
# an incomplete archive index, which surfaces much later as undefined references.
#
# ASM is set explicitly because rlottie declares LANGUAGES C CXX ASM for its NEON
# raster paths, and CMake will not always infer the cross assembler.
write_toolchain_file() {
    local out="$1"

    cat > "$out" <<TOOLCHAIN
SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_PROCESSOR arm)

# Note: do NOT force _GLIBCXX_USE_C99_MATH_FUNCS on here to get std::lround. That
# guard also covers the long double variants, and the Harmattan glibc does not
# declare acoshl/asinhl/... at all (on ARM EABI long double is double, and old glibc
# gates them behind __NO_LONG_DOUBLE_MATH). Forcing it just moves the failure to
# "'acoshl' has not been declared in '::'". libstdc++'s probe is right; the affected
# rlottie call sites are patched in build_rlottie instead.

SET(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
SET(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
SET(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}-gcc)

SET(CMAKE_AR      ${TOOLCHAIN_PREFIX}-gcc-ar     CACHE FILEPATH "" FORCE)
SET(CMAKE_RANLIB  ${TOOLCHAIN_PREFIX}-gcc-ranlib CACHE FILEPATH "" FORCE)
SET(CMAKE_NM      ${TOOLCHAIN_PREFIX}-gcc-nm     CACHE FILEPATH "" FORCE)

SET(CMAKE_C_COMPILER_AR       ${TOOLCHAIN_PREFIX}-gcc-ar)
SET(CMAKE_CXX_COMPILER_AR     ${TOOLCHAIN_PREFIX}-gcc-ar)
SET(CMAKE_C_COMPILER_RANLIB   ${TOOLCHAIN_PREFIX}-gcc-ranlib)
SET(CMAKE_CXX_COMPILER_RANLIB ${TOOLCHAIN_PREFIX}-gcc-ranlib)
TOOLCHAIN
}

# SHA256 checksum verification
check_sha256() {
    local expected_hash="$1"
    local file="$2"

    info "Verifying SHA256 checksum for $file..."
    local actual_hash
    actual_hash=$(sha256sum "$file" | awk '{print $1}')
    
    if [ "$actual_hash" != "$expected_hash" ]; then
        error "SHA256 checksum mismatch for $file. Expected $expected_hash, got $actual_hash."
        exit 1
    fi

    success "SHA256 checksum verified for $file."
}

# Build OpenSSL
build_openssl() {
    # 3.5.x is the current LTS branch (supported to 2030-04-08). The previous pin,
    # 3.3.1, sat on the 3.3 branch which went end-of-support on 2026-04-09 and so
    # never received the June 2026 high-severity fixes.
    local version="3.5.7"
    local hash="a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8"
    local filename="openssl-$version.tar.gz"
    local stamp="build/crypto/.built-version"

    # Only the download and extraction used to be skipped on a re-run: ./Configure
    # regenerates the makefiles, so make then rebuilt everything anyway. The stamp
    # records which version is installed, so bumping the version still rebuilds.
    if [ -z "${FORCE_REBUILD:-}" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$version" ]; then
        warn "OpenSSL $version already built in build/crypto, skipping. FORCE_REBUILD=1 to redo."
        return
    fi

    if [ -d "openssl-$version" ]; then
        warn "OpenSSL directory already exists, skipping download and extraction."
    else
        if [ ! -f "$filename" ]; then
            info "Downloading OpenSSL sources..."
            wget "https://www.openssl.org/source/$filename"
        fi
        
        check_sha256 "$hash" "$filename"

        info "Extracting OpenSSL sources..."
        tar xzf "$filename"
    fi

    cd "openssl-$version"

    info "Configuring OpenSSL..."
    if [[ "$ARGS" == "harmattan" ]]; then
        ./Configure --cross-compile-prefix="$TOOLCHAIN_PREFIX-" linux-generic32 shared no-unit-test
    else
        ./config shared
    fi

    info "Building OpenSSL..."
    make depend
    make -j"$JOBS"

    info "Installing OpenSSL..."
    mkdir -p ../build/crypto/lib
    cp libcrypto.so* libssl.so* ../build/crypto/lib
    cp -r include ../build/crypto

    cd ..
    echo "$version" > "$stamp"
    success "OpenSSL built successfully."
}

# Build ZLib
build_zlib() {
    local version="1.3.2"
    local hash="d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3"
    local filename="zlib-$version.tar.xz"
    local stamp="build/zlib/.built-version"

    if [ -z "${FORCE_REBUILD:-}" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$version" ]; then
        warn "ZLib $version already built in build/zlib, skipping. FORCE_REBUILD=1 to redo."
        return
    fi

    if [ -d "zlib-$version" ]; then
        warn "ZLib directory already exists, skipping download and extraction."
    else
        if [ ! -f "$filename" ]; then
            info "Downloading ZLib sources..."
            wget "https://www.zlib.net/$filename"
        fi

        check_sha256 "$hash" "$filename"

        info "Extracting ZLib sources..."
        tar xJf "$filename"
    fi

    cd "zlib-$version"

    info "Configuring ZLib..."
    if [[ "$ARGS" == "harmattan" ]]; then
        CC="$TOOLCHAIN_PREFIX-gcc" CFLAGS="-fPIC" ./configure --shared
    else
        CFLAGS="-fPIC" ./configure --shared
    fi

    info "Building ZLib..."
    make -j"$JOBS"

    info "Installing ZLib..."
    mkdir -p ../build/zlib/lib ../build/zlib/include
    cp libz.so* ../build/zlib/lib
    cp zconf.h zlib.h ../build/zlib/include

    cd ..
    echo "$version" > "$stamp"
    success "ZLib built successfully."
}

# Build rlottie
#
# CMakeLists.txt has find_package(rlottie REQUIRED) but nothing here used to build
# it, so configuring the app failed on a fresh machine. rlottie renders the .tgs
# sticker animations on the authentication pages (src/LottieAnimation.cpp).
build_webp() {
    local stamp="build/libwebp/.built-commit"
    local want="$LIBWEBP_TAG:$ARGS"

    if [ -z "${FORCE_REBUILD:-}" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ]; then
        warn "libwebp already built and installed for $ARGS, skipping. FORCE_REBUILD=1 to redo."
        return
    fi

    if [ -d "libwebp" ]; then
        warn "libwebp checkout already exists, skipping fetch. Remove libwebp/ to re-pin."
    else
        info "Fetching libwebp $LIBWEBP_TAG..."

        git clone --quiet --depth=1 --branch "$LIBWEBP_TAG" https://chromium.googlesource.com/webm/libwebp libwebp
    fi

    rm -rf build/libwebp
    mkdir -p build/libwebp

    local webp_root
    webp_root=$(realpath libwebp)

    # Decoder only. Nothing here encodes WebP, and the CLI tools would drag in libpng,
    # libjpeg and giflib - none of which are in the sysroot.
    #
    # POSITION_INDEPENDENT_CODE for the same reason as rlottie: meegram links -pie and
    # a non-PIC static archive will not go into a PIE executable on ARM.
    #
    # SIMD is left on. The toolchain is built --with-fpu=neon so libwebp's NEON paths
    # compile from the compiler's own __ARM_NEON__, with no equivalent of rlottie's
    # -DARCH=arm needed.
    local webp_options=(
        -DCMAKE_BUILD_TYPE=MinSizeRel
        -DCMAKE_INSTALL_PREFIX=/usr/local
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DWEBP_BUILD_ANIM_UTILS=OFF
        -DWEBP_BUILD_CWEBP=OFF
        -DWEBP_BUILD_DWEBP=OFF
        -DWEBP_BUILD_GIF2WEBP=OFF
        -DWEBP_BUILD_IMG2WEBP=OFF
        -DWEBP_BUILD_VWEBP=OFF
        -DWEBP_BUILD_WEBPINFO=OFF
        -DWEBP_BUILD_WEBPMUX=OFF
        -DWEBP_BUILD_EXTRAS=OFF
    )

    if [[ "$ARGS" == "harmattan" ]]; then
        info "Configuring libwebp for Harmattan..."
        write_toolchain_file "$webp_root/toolchain.cmake"

        cmake -B build/libwebp -S "$webp_root" \
            -DCMAKE_TOOLCHAIN_FILE="$webp_root/toolchain.cmake" \
            "${webp_options[@]}"
    else
        info "Configuring libwebp..."
        cmake -B build/libwebp -S "$webp_root" "${webp_options[@]}"
    fi

    info "Building libwebp..."
    cmake --build build/libwebp --parallel "$JOBS"

    if [[ "$ARGS" == "harmattan" ]]; then
        info "Installing libwebp to Harmattan SDK..."
        DESTDIR="$SDK_PATH/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim" cmake --install build/libwebp
    else
        cmake --install build/libwebp
    fi

    echo "$want" > "$stamp"

    success "libwebp built and installed successfully."
}

# Build libogg and libopus
#
# Voice messages, in both directions: the encoder for what the microphone records and the
# decoder for what arrives. Static, like everything else here, because a device from 2012
# will never have these in its image and shipping .so files into /opt would mean an
# rpath and a packaging step for two libraries that together come to a few hundred KB.
build_opus() {
    local stamp="build/opus/.built-commit"
    local want="$LIBOGG_TAG+$OPUS_TAG:$ARGS"

    if [ -z "${FORCE_REBUILD:-}" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ]; then
        warn "libogg/libopus already built and installed for $ARGS, skipping. FORCE_REBUILD=1 to redo."
        return
    fi

    if [ -d "libogg" ]; then
        warn "libogg checkout already exists, skipping fetch. Remove libogg/ to re-pin."
    else
        info "Fetching libogg $LIBOGG_TAG..."

        git clone --quiet --depth=1 --branch "$LIBOGG_TAG" https://github.com/xiph/ogg libogg
    fi

    if [ -d "opus" ]; then
        warn "opus checkout already exists, skipping fetch. Remove opus/ to re-pin."
    else
        info "Fetching opus $OPUS_TAG..."

        git clone --quiet --depth=1 --branch "$OPUS_TAG" https://github.com/xiph/opus opus
    fi

    local ogg_root opus_root
    ogg_root=$(realpath libogg)
    opus_root=$(realpath opus)

    # POSITION_INDEPENDENT_CODE for the same reason as rlottie and libwebp: meegram links
    # -pie and a non-PIC static archive will not go into a PIE executable on ARM.
    #
    # CMAKE_POLICY_VERSION_MINIMUM for the same reason as rlottie: libogg 1.3.5 declares
    # cmake_minimum_required(VERSION 3.0) and opus 1.4 declares 3.1, and CMake 4.x removed
    # compatibility below 3.5, so both fail to configure outright without it. Harmless on
    # older CMake, which just reports it as an unused variable.
    local common_options=(
        -DCMAKE_BUILD_TYPE=MinSizeRel
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DCMAKE_INSTALL_PREFIX=/usr/local
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_TESTING=OFF
    )

    # Fixed point on the device. The Cortex-A8's VFP unit is not pipelined and scalar
    # float is slow on it - NEON is the fast path but the compiler will not reliably put
    # opus's scalar float code there. Encoding has to keep up with the microphone in real
    # time while the UI still draws, so the integer path is the safer default; it is what
    # every other ARMv7 opus build uses. Override with OPUS_FIXED_POINT=OFF to A/B it.
    #
    # Left off for the simulator, where it would only cost quality for no reason.
    local opus_fixed_point="${OPUS_FIXED_POINT:-}"

    if [ -z "$opus_fixed_point" ]; then
        if [[ "$ARGS" == "harmattan" ]]; then
            opus_fixed_point=ON
        else
            opus_fixed_point=OFF
        fi
    fi

    # opus_demo and the test vectors are not wanted; nothing here runs them.
    local opus_options=(
        "${common_options[@]}"
        -DOPUS_BUILD_PROGRAMS=OFF
        -DOPUS_BUILD_TESTING=OFF
        -DOPUS_BUILD_SHARED_LIBRARY=OFF
        -DOPUS_FIXED_POINT="$opus_fixed_point"
    )

    local sysroot="$SDK_PATH/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim"

    for lib in ogg opus; do
        local root options
        if [ "$lib" = "ogg" ]; then
            root="$ogg_root"
            options=("${common_options[@]}")
        else
            root="$opus_root"
            options=("${opus_options[@]}")
        fi

        rm -rf "build/$lib"
        mkdir -p "build/$lib"

        if [[ "$ARGS" == "harmattan" ]]; then
            info "Configuring lib$lib for Harmattan..."
            write_toolchain_file "$root/toolchain.cmake"

            cmake -B "build/$lib" -S "$root" \
                -DCMAKE_TOOLCHAIN_FILE="$root/toolchain.cmake" \
                "${options[@]}"
        else
            info "Configuring lib$lib..."
            cmake -B "build/$lib" -S "$root" "${options[@]}"
        fi

        info "Building lib$lib..."
        cmake --build "build/$lib" --parallel "$JOBS"

        if [[ "$ARGS" == "harmattan" ]]; then
            info "Installing lib$lib to Harmattan SDK..."
            DESTDIR="$sysroot" cmake --install "build/$lib"
        else
            cmake --install "build/$lib"
        fi
    done

    mkdir -p build/opus
    echo "$want" > "$stamp"

    success "libogg and libopus built and installed successfully."
}

build_rlottie() {
    local stamp="build/rlottie/.built-commit"
    local want="$RLOTTIE_COMMIT:$ARGS"

    if [ -z "${FORCE_REBUILD:-}" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ]; then
        warn "rlottie already built and installed for $ARGS, skipping. FORCE_REBUILD=1 to redo."
        return
    fi

    if [ -d "rlottie" ]; then
        warn "rlottie checkout already exists, skipping fetch. Remove rlottie/ to re-pin."
    else
        info "Fetching rlottie at $RLOTTIE_COMMIT..."

        # Same shallow-fetch-a-pinned-commit approach as TDLib. rlottie's newest tag
        # is v0.2 from 2020, so a tag is not a useful pin.
        mkdir -p rlottie
        git -C rlottie init --quiet
        git -C rlottie remote add origin https://github.com/Samsung/rlottie
        git -C rlottie fetch --quiet --depth=1 origin "$RLOTTIE_COMMIT"
        git -C rlottie checkout --quiet FETCH_HEAD
    fi

    # std::lround does not exist against this sysroot: libstdc++ leaves the whole
    # C99 math block out of namespace std because the Harmattan glibc lacks the long
    # double variants. ::lround is declared and works, so rewrite the call sites.
    #
    # Idempotent by construction - once rewritten there is no std::lround left to
    # match, so a re-run is a no-op rather than an error.
    local patched=0
    for f in src/vector/vmatrix.cpp src/lottie/lottieparser.cpp; do
        if [ -f "rlottie/$f" ] && grep -q "std::lround" "rlottie/$f"; then
            sed -i 's/std::lround/lround/g' "rlottie/$f"
            patched=$((patched + 1))
        fi
    done

    if [ "$patched" -gt 0 ]; then
        info "Rewrote std::lround -> lround in $patched rlottie file(s)."
    fi

    rm -rf build/rlottie
    mkdir -p build/rlottie

    local rlottie_root
    rlottie_root=$(realpath rlottie)

    # LOTTIE_MODULE=OFF   - dlopens a separate image-loader .so at runtime. Nothing
    #                       uses it here and it would mean shipping a second library.
    # LOTTIE_THREAD=OFF   - LottieAnimation calls renderSync() on the GUI thread and
    #                       the N9 is a single-core Cortex-A8, so render threads buy
    #                       nothing and cost memory.
    # BUILD_SHARED_LIBS=OFF - static, so there is no extra .so to deploy alongside
    #                       the binary.
    # POSITION_INDEPENDENT_CODE - required: meegram links -pie, and a non-PIC static
    #                       archive fails to link into a PIE executable on ARM.
    #
    # CMAKE_POLICY_VERSION_MINIMUM - rlottie still declares
    # cmake_minimum_required(VERSION 3.3), and CMake 4.x removed compatibility below
    # 3.5, so configuring fails outright without this. It also covers rlottie's
    # bundled freetype/pixman/stb subprojects. Harmless on older CMake, which just
    # reports it as an unused variable. TDLib declares 3.10 and needs no such help.
    # LIB_INSTALL_DIR is pinned because rlottie defaults it to a bare "/usr/lib"
    # while installing its headers to a *relative* "include", i.e. under
    # CMAKE_INSTALL_PREFIX. Left alone the install splits itself across
    # <sysroot>/usr/lib and <sysroot>/usr/local/include.
    #
    # It must be RELATIVE. An absolute install DESTINATION is baked verbatim into
    # the generated rlottieTargets.cmake, so the imported target points at
    # /usr/local/lib/librlottie.a on the host rather than inside the sysroot, and
    # find_package fails with "references the file ... but this file does not
    # exist". Relative paths are resolved against _IMPORT_PREFIX, which CMake
    # derives from wherever the config file is actually found.
    local rlottie_options=(
        -DCMAKE_BUILD_TYPE=MinSizeRel
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DCMAKE_INSTALL_PREFIX=/usr/local
        -DLIB_INSTALL_DIR=lib
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DLOTTIE_MODULE=OFF
        -DLOTTIE_THREAD=OFF
        -DLOTTIE_TEST=OFF
    )

    if [[ "$ARGS" == "harmattan" ]]; then
        info "Configuring rlottie for Harmattan..."
        write_toolchain_file "$rlottie_root/toolchain.cmake"

        # -DARCH=arm is required, not optional. src/vector/pixman/CMakeLists.txt
        # adds pixman-arm-neon-asm.S only under IF("${ARCH}" STREQUAL "arm"), and
        # ARCH is never assigned anywhere in rlottie - it has to come from here.
        # Meanwhile vdrawhelper_neon.cpp compiles its NEON path whenever the
        # compiler defines __ARM_NEON__, which ours does by default (built
        # --with-fpu=neon). Without ARCH you get the calls but not the assembly:
        # "undefined reference to pixman_composite_over_n_8888_asm_neon".
        # Setting it also buys the NEON raster fast paths on the Cortex-A8.
        cmake -B build/rlottie -S "$rlottie_root" \
            -DCMAKE_TOOLCHAIN_FILE="$rlottie_root/toolchain.cmake" \
            -DARCH=arm \
            "${rlottie_options[@]}"
    else
        info "Configuring rlottie..."
        cmake -B build/rlottie -S "$rlottie_root" "${rlottie_options[@]}"
    fi

    # Only the library. rlottie's CMakeLists adds example/ unconditionally - there is
    # no option for it - and lottie2gif is dead weight here. Building the named
    # target skips it.
    info "Building rlottie..."
    cmake --build build/rlottie --target rlottie --parallel "$JOBS"

    # cmake --install, not make install: the generated `install` target depends on
    # `all`, which would build example/lottie2gif again and undo the --target above.
    # This installs what is already built. example/ has no install rules, so nothing
    # is missing.
    if [[ "$ARGS" == "harmattan" ]]; then
        info "Installing rlottie to Harmattan SDK..."
        DESTDIR="$SDK_PATH/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim" cmake --install build/rlottie
    else
        cmake --install build/rlottie
    fi

    echo "$want" > "$stamp"

    success "rlottie built and installed successfully."
}

# TDLib's JSON converter never implemented to_json for vector<bytes>: it emits the
# literal placeholder "UNSUPPORTED STORED VECTOR OF BYTES", which is not valid C++.
# Upstream never trips it, because server mode only emits to_json for Objects and no
# Object has such a field. Client mode emits to_json for queries, and two of those do -
# inputPassportElementErrorSourceFiles and ...TranslationFiles, both file_hashes. So
# generating the client-direction codec (below) requires implementing the missing case.
#
# Not target-specific, unlike TD_HAS_MMSG: this one is about what we generate, not what
# the N9 can run. Same idempotence rule, for the same reason - td/ is reused across runs.
patch_td_json_vector_bytes() {
    local header="td/td/tl/tl_json.h"
    local converter="td/td/generate/tl_json_converter.cpp"

    # The helper goes next to the JsonVectorInt64 it mirrors. tl_json.h is already
    # included by every generated file, so nothing needs a new include.
    if grep -q "JsonVectorBytes" "$header"; then
        info "JsonVectorBytes already present in tl_json.h."
    elif grep -q "^struct JsonVectorInt64 {$" "$header"; then
        local block
        block=$(mktemp)
        cat > "$block" <<'EOF'
struct JsonVectorBytes {
  const vector<string> &value;
};

inline void to_json(JsonValueScope &jv, const JsonVectorBytes &vec) {
  auto ja = jv.enter_array();
  for (auto &value : vec.value) {
    ja.enter_value() << base64_encode(value);
  }
}

EOF
        # getline rather than sed, so nothing in the block is escape-processed.
        awk -v f="$block" '
            /^struct JsonVectorInt64 {$/ && !done {
                while ((getline line < f) > 0) print line
                done = 1
            }
            { print }
        ' "$header" > "$header.new" && mv "$header.new" "$header"
        rm -f "$block"
        success "Added JsonVectorBytes to tl_json.h."
    else
        error "No 'struct JsonVectorInt64 {' anchor in $header."
        error "TDLib has restructured tl_json.h; the vector<bytes> patch needs revisiting."
        exit 1
    fi

    if grep -q "JsonVectorBytes{" "$converter"; then
        info "Converter already emits JsonVectorBytes."
    elif grep -q "UNSUPPORTED STORED VECTOR OF BYTES" "$converter"; then
        sed -i 's|object = "UNSUPPORTED STORED VECTOR OF BYTES";|object = PSTRING() << "JsonVectorBytes{" << object << "}";|' \
            "$converter"
        # sed exits 0 on no match, and a silent miss here surfaces much later as
        # 'UNSUPPORTED was not declared in this scope' in a generated file.
        if ! grep -q "JsonVectorBytes{" "$converter"; then
            error "Found the placeholder in $converter but could not rewrite it."
            error "The surrounding line has changed; the vector<bytes> patch needs revisiting."
            exit 1
        fi
        success "Converter now emits JsonVectorBytes for vector<bytes>."
    else
        error "Found neither the placeholder nor JsonVectorBytes in $converter."
        error "TDLib has likely implemented vector<bytes> itself; the patch needs revisiting."
        exit 1
    fi
}

# Build TDLib
build_tdlib() {
    if [ -d "td" ]; then
        warn "TDLib checkout already exists, skipping fetch. Remove td/ to re-pin."
    else
        info "Fetching TDLib at $TDLIB_COMMIT..."

        # Shallow-fetch the pinned commit. This used to be `git clone --depth=1`,
        # which takes whatever master happens to be that day - so no two builds
        # were guaranteed to use the same TDLib.
        mkdir -p td
        git -C td init --quiet
        git -C td remote add origin https://github.com/tdlib/td
        git -C td fetch --quiet --depth=1 origin "$TDLIB_COMMIT"
        git -C td checkout --quiet FETCH_HEAD
    fi

    if [[ "$ARGS" == "harmattan" ]]; then
        local config_header="td/tdutils/td/utils/port/config.h"

        # The N9's kernel predates recvmmsg/sendmmsg. sed exits 0 even when it
        # matches nothing, so verify the macro is actually there - otherwise a TDLib
        # restructure would silently produce a binary that fails on the device.
        #
        # The patch has to be idempotent: td/ is reused across runs, so on a second
        # run the macro is already 0 and "no match" means "already done", not
        # "upstream changed".
        if grep -q "TD_HAS_MMSG 0" "$config_header"; then
            info "TD_HAS_MMSG already disabled for Harmattan."
        elif grep -q "TD_HAS_MMSG 1" "$config_header"; then
            sed -i 's/TD_HAS_MMSG 1/TD_HAS_MMSG 0/g' "$config_header"
            success "Disabled TD_HAS_MMSG for Harmattan."
        else
            error "Found neither 'TD_HAS_MMSG 1' nor 'TD_HAS_MMSG 0' in $config_header."
            error "TDLib has likely moved or renamed it; the Harmattan patch needs revisiting."
            exit 1
        fi
    fi

    patch_td_json_vector_bytes

    # Written only after install succeeds, so a failed run always rebuilds. Keyed on
    # everything that changes the output: the pinned commit, the target, and whether
    # LTO is on. Bumping any of them therefore forces a rebuild rather than silently
    # reusing the previous one.
    local stamp="build/tdlib/.built-commit"
    local want="$TDLIB_COMMIT:$ARGS:$TDLIB_LTO"

    if [ -z "${FORCE_REBUILD:-}" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ]; then
        warn "TDLib already built and installed for $ARGS, skipping. FORCE_REBUILD=1 to redo."
        return
    fi

    # Deliberately a clean wipe: a stale CMake cache in a cross-build is a good way
    # to get a subtly wrong binary. The stamp above is what makes re-runs cheap.
    rm -rf build/generate build/tdlib
    mkdir -p build/generate build/tdlib

    local td_root=$(realpath td)
    local zlib_root=$(realpath build/zlib)
    local openssl_root=$(realpath build/crypto)
    local openssl_crypto_lib="$openssl_root/lib/libcrypto.so"
    local openssl_ssl_lib="$openssl_root/lib/libssl.so"
    local zlib_lib="$zlib_root/lib/libz.so"

    # Arrays, not strings. The previous form embedded literal quote characters via
    # \" and was then expanded unquoted: word splitting applies to that, but quote
    # removal does not, so CMake received paths like "/build/crypto/include" with
    # the quotes as part of the value and rejected them as relative paths.
    # Arrays also survive spaces in the checkout path.
    local openssl_options=(
        -DOPENSSL_FOUND=1
        -DOPENSSL_INCLUDE_DIR="$openssl_root/include"
        -DOPENSSL_CRYPTO_LIBRARY="$openssl_crypto_lib"
        -DOPENSSL_SSL_LIBRARY="$openssl_ssl_lib"
    )

    local zlib_options=(
        -DZLIB_FOUND=1
        -DZLIB_LIBRARIES="$zlib_lib"
        -DZLIB_INCLUDE_DIR="$zlib_root/include"
    )

    if [[ "$ARGS" == "harmattan" ]]; then
        info "Setting up cross-compilation for Harmattan..."
        write_toolchain_file "$td_root/toolchain.cmake"
    fi

    if [[ "$ARGS" == "harmattan" ]]; then
        cd build/generate
        cmake "$td_root"
        cd ../..

        cd build/tdlib
        cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DTD_ENABLE_LTO="$TDLIB_LTO" -DCMAKE_TOOLCHAIN_FILE="$td_root/toolchain.cmake" \
            "${openssl_options[@]}" "${zlib_options[@]}" "$td_root"
        cd ../..

        info "Generating TDLib autogenerated source files..."
        cmake --build build/generate --target prepare_cross_compiling --parallel "$JOBS"
        info "Building TDLib for Harmattan..."
        cmake --build build/tdlib --parallel "$JOBS"
    else
        cd build/tdlib
        cmake -DCMAKE_BUILD_TYPE=Release "${openssl_options[@]}" "${zlib_options[@]}" "$td_root"
        cmake --build . --parallel "$JOBS"
        cd ../..
    fi

    # -C build/tdlib, not a bare `make`. The Harmattan branch returns to the source
    # directory after configuring, so a bare `make install` ran here and failed with
    # "No rule to make target 'install'" - there is no Makefile at the top level.
    # The simulator branch only worked by accident, because it never undid its cd.
    if [[ "$ARGS" == "harmattan" ]]; then
        info "Installing TDLib to Harmattan SDK..."
        make -C build/tdlib DESTDIR="$SDK_PATH/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim" install
    else
        make -C build/tdlib install
    fi

    echo "$want" > "$stamp"

    success "TDLib built and installed successfully."
}

# TDLib generates its JSON codec in the server direction only - to_json for results,
# from_json for requests. A UI talking to meegramd needs the mirror, and TDLib ships
# neither half of it (docs/restructuring.md). tools/generate_json_client.cpp re-runs
# TDLib's own generator entry point in client mode to produce it.
#
# Host-compiled by hand rather than added to TDLib's CMake: it is a build-time tool that
# must never be cross-compiled, and keeping it here holds the td/ diff to the two lines
# patch_td_json_vector_bytes applies. Needed only for -DMEEGRAM_JSON_TRANSPORT=ON, but
# generated unconditionally - it costs a few seconds and being absent is a confusing
# configure-time failure.
generate_json_client_codec() {
    local td_root=$(realpath td)
    local auto_dir="$td_root/td/generate/auto"
    local tlo="$auto_dir/tlo/td_api.tlo"
    local header="$auto_dir/td/telegram/td_api_json_client.h"

    if [ ! -f "$tlo" ]; then
        error "No $tlo - TDLib's own generators have not run."
        return 1
    fi

    # Regenerate only when the schema is newer than what was generated from it. The
    # generator itself rewrites files only when their contents change, so this is about
    # skipping the compile, not the write.
    #
    # tl_json_converter.cpp is in the list because patch_td_json_vector_bytes rewrites
    # it: without this, applying the patch to an already-generated tree would leave the
    # placeholder standing in the output.
    if [ -z "${FORCE_REBUILD:-}" ] && [ -f "$header" ] && [ "$header" -nt "$tlo" ] &&
        [ "$header" -nt tools/generate_json_client.cpp ] &&
        [ "$header" -nt "$td_root/td/generate/tl_json_converter.cpp" ]; then
        warn "Client-direction JSON codec is up to date, skipping."
        return
    fi

    # A Harmattan cross-build keeps its host-side generator tree in build/generate; a
    # native build has everything in build/tdlib. Either way these are host binaries.
    local host_tree="build/generate"
    if [ ! -f "$host_tree/tdtl/libtdtl.a" ]; then
        host_tree="build/tdlib"
    fi

    if [ ! -f "$host_tree/tdtl/libtdtl.a" ] || [ ! -f "$host_tree/tdutils/libtdutils.a" ]; then
        error "No host-built libtdtl.a/libtdutils.a under build/. Cannot generate the JSON codec."
        return 1
    fi

    info "Generating the client-direction TDLib JSON codec..."

    local generator
    generator=$(realpath "$host_tree")/generate_json_client

    # tl_json_converter.cpp is compiled in rather than linked: TDLib builds it straight
    # into its generate_json executable and never archives it into a library.
    "${HOSTCXX:-g++}" -std=c++17 -O1 -o "$generator" \
        tools/generate_json_client.cpp \
        "$td_root/td/generate/tl_json_converter.cpp" \
        -I"$td_root/td/generate" -I"$td_root/tdtl" -I"$td_root/tdutils" -I"$(realpath "$host_tree")/tdutils" \
        "$(realpath "$host_tree")/tdtl/libtdtl.a" "$(realpath "$host_tree")/tdutils/libtdutils.a" \
        -lcrypto -lz -lpthread

    # Paths inside the generator are relative to td/generate/auto, which is where TDLib
    # runs its own generators from.
    (cd "$auto_dir" && "$generator")

    success "Client-direction JSON codec generated."
}

# Main function to run the build process
main() {
    validate_args
    check_harmattan_env
    build_zlib
    build_openssl
    build_rlottie
    build_webp
    build_opus
    build_tdlib
    generate_json_client_codec
}

main "$@"
