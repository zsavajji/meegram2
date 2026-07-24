#!/usr/bin/env bash

# Builds a modern arm-none-linux-gnueabi cross-toolchain targeting the Harmattan
# sysroot shipped with the Qt SDK.
#
# Why this is needed: MADDE's stock compiler is GCC 4.4.1, which predates C++11.
# MeeGram is C++23 (std::jthread, std::ranges, std::to_array), so it needs a
# current GCC. Only the *compiler* is replaced - CMakeLists.txt links
# -static-libstdc++, so the device's 2011 libstdc++ is bypassed. glibc stays
# dynamic, which is why we build against the sysroot's glibc rather than a new one.
#
# Usage:
#   export QT_SDK_PATH=/home/you/QtSDK
#   ./tools/build-toolchain.sh              # builds into ~/cross/arm-harmattan
#   PREFIX=/opt/arm-harmattan ./tools/build-toolchain.sh
#
# Afterwards:
#   export PATH="$PREFIX/bin:$PATH"
#   export TOOLCHAIN_PREFIX=arm-none-linux-gnueabi
#
# Expect this to take a while and to need a couple of GB of disk.

set -euo pipefail

declare -r COLOR_RESET="\033[0m"
declare -r COLOR_RED="\033[31m"
declare -r COLOR_GREEN="\033[32m"
declare -r COLOR_BLUE="\033[34m"

info()    { echo -e "${COLOR_BLUE}INFO: $*${COLOR_RESET}"; }
success() { echo -e "${COLOR_GREEN}SUCCESS: $*${COLOR_RESET}"; }
error()   { echo -e "${COLOR_RED}ERROR: $*${COLOR_RESET}" >&2; }

readonly TARGET="${TARGET:-arm-none-linux-gnueabi}"
readonly PREFIX="${PREFIX:-$HOME/cross/arm-harmattan}"
readonly WORKDIR="${WORKDIR:-$PWD/toolchain-build}"

readonly BINUTILS_VERSION="${BINUTILS_VERSION:-2.42}"
readonly GCC_VERSION="${GCC_VERSION:-14.2.0}"

# ARM ABI for the N9's OMAP3630, matching CMakeLists.txt's BUILD_HARMATTAN flags.
#
# hard, not softfp: the Harmattan sysroot's libc reports
#   Tag_ABI_VFP_args: VFP registers
# Confirm on your own sysroot with:
#   readelf -A "$SYSROOT/lib/libc.so.6" | grep Tag_ABI_VFP_args
# If that ever reads "base variant" instead, set FLOAT_ABI=softfp here and change
# -mfloat-abi in CMakeLists.txt to match - the two must agree.
readonly ARM_ARCH="${ARM_ARCH:-armv7-a}"
readonly ARM_FPU="${ARM_FPU:-neon}"
readonly FLOAT_ABI="${FLOAT_ABI:-hard}"

resolve_sysroot() {
    if [[ -n "${SYSROOT:-}" ]]; then
        return
    fi

    if [[ -z "${QT_SDK_PATH:-}" ]]; then
        error "Set QT_SDK_PATH (or SYSROOT) before running this script."
        exit 1
    fi

    SYSROOT="$QT_SDK_PATH/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim"
}

check_sysroot() {
    if [[ ! -d "$SYSROOT" ]]; then
        error "Sysroot not found: $SYSROOT"
        exit 1
    fi

    if [[ ! -f "$SYSROOT/usr/include/features.h" ]]; then
        error "$SYSROOT has no usr/include/features.h - this looks like a runtime-only"
        error "sysroot without development headers. GCC cannot be built against it."
        exit 1
    fi

    info "Sysroot: $SYSROOT"
    info "glibc:   $(ls "$SYSROOT"/lib/libc-*.so 2>/dev/null || echo 'unknown')"
}

check_host_tools() {
    local missing=()

    for tool in gcc g++ make flex bison makeinfo wget tar; do
        command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
    done

    if [ ${#missing[@]} -gt 0 ]; then
        error "Missing host tools: ${missing[*]}"
        error "Debian/Ubuntu: sudo apt install build-essential flex bison texinfo wget"
        exit 1
    fi
}

fetch() {
    local url="$1" filename="$2"

    if [ -f "$filename" ]; then
        info "$filename already downloaded."
    else
        info "Downloading $filename..."
        wget -q --show-progress "$url"
    fi
}

build_binutils() {
    local name="binutils-$BINUTILS_VERSION"

    fetch "https://ftp.gnu.org/gnu/binutils/$name.tar.xz" "$name.tar.xz"
    [ -d "$name" ] || tar xf "$name.tar.xz"

    info "Configuring binutils..."
    rm -rf build-binutils && mkdir build-binutils && cd build-binutils

    "../$name/configure" \
        --target="$TARGET" \
        --prefix="$PREFIX" \
        --with-sysroot="$SYSROOT" \
        --disable-nls \
        --disable-werror

    info "Building binutils..."
    make -j"$(nproc)"
    make install

    cd ..
    success "binutils built."
}

build_gcc() {
    local name="gcc-$GCC_VERSION"

    fetch "https://ftp.gnu.org/gnu/gcc/$name/$name.tar.xz" "$name.tar.xz"
    [ -d "$name" ] || tar xf "$name.tar.xz"

    info "Fetching GCC prerequisites (gmp, mpfr, mpc, isl)..."
    (cd "$name" && ./contrib/download_prerequisites >/dev/null)

    info "Configuring GCC..."
    rm -rf build-gcc && mkdir build-gcc && cd build-gcc

    # --disable-libsanitizer: libsanitizer does not build against a 2011-era glibc,
    #   and CMakeLists.txt already keeps ASan out of Harmattan builds.
    # --with-arch/fpu/float: make the compiler defaults agree with the flags
    #   CMakeLists.txt passes for BUILD_HARMATTAN.
    "../$name/configure" \
        --target="$TARGET" \
        --prefix="$PREFIX" \
        --with-sysroot="$SYSROOT" \
        --enable-languages=c,c++ \
        --enable-threads=posix \
        --disable-multilib \
        --disable-nls \
        --disable-libsanitizer \
        --disable-libstdcxx-pch \
        --with-arch="$ARM_ARCH" \
        --with-fpu="$ARM_FPU" \
        --with-float="$FLOAT_ABI"

    info "Building GCC (this is the long part)..."
    make -j"$(nproc)"
    make install

    cd ..
    success "GCC built."
}

verify() {
    export PATH="$PREFIX/bin:$PATH"

    info "Verifying toolchain..."
    "$TARGET-g++" --version | head -1

    # Exercises the three things MeeGram actually needs from a modern toolchain:
    # std::to_array (src/Emoji.cpp), std::ranges (src/ChatModel.cpp), and
    # std::jthread (src/Client.cpp). jthread is the one worth checking - it needs
    # libstdc++ built with thread support, which is the usual casualty of a
    # misconfigured cross build, and it only shows up at link time.
    local probe="$WORKDIR/probe.cpp"
    cat > "$probe" <<'CPP'
#include <algorithm>
#include <array>
#include <ranges>
#include <thread>
#include <vector>

int main()
{
    constexpr auto values = std::to_array({3, 1, 2});
    std::vector<int> v(values.begin(), values.end());

    std::jthread worker([&v] { std::ranges::sort(v); });
    worker.join();

    return static_cast<int>(std::ranges::count(v, 42));
}
CPP

    "$TARGET-g++" -std=c++23 -pthread "$probe" -o "$WORKDIR/probe"
    file "$WORKDIR/probe"

    success "Toolchain works and compiles C++23 for ARM."
    echo
    echo "Add to your shell:"
    echo "  export PATH=\"$PREFIX/bin:\$PATH\""
    echo "  export TOOLCHAIN_PREFIX=$TARGET"
}

main() {
    resolve_sysroot
    check_sysroot
    check_host_tools

    mkdir -p "$WORKDIR"
    cd "$WORKDIR"

    build_binutils
    build_gcc
    verify
}

main "$@"
