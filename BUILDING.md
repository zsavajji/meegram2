# Building MeeGram

MeeGram targets the **Nokia N9** running MeeGo 1.2 Harmattan: 1 GHz Cortex-A8
(OMAP3630), PowerVR SGX530, 1 GB RAM, 854×480, **Qt 4.7.4 with QtDeclarative
(QML1)**. It is built with CMake and cross-compiled from Linux; Qt Creator is
useful for editing and for deploying to the device, but it does not drive the build.

---

## The one thing that will block you

The Qt SDK's bundled MADDE toolchain is **GCC 4.4.1**, which predates C++11.
This codebase is **C++23** (`CMAKE_CXX_STANDARD 23`) and uses `std::jthread`
(`src/Client.cpp`), `std::ranges`, and `std::to_array` (`src/Emoji.cpp`).

**You cannot build MeeGram with the stock SDK compiler**, and no combination of
flags changes that. You need a modern `arm-none-linux-gnueabi` GCC targeting the
Harmattan sysroot. `tools/build-toolchain.sh` builds one.

Only the *compiler* has to be modern. `CMakeLists.txt` links `-static-libstdc++`,
so the C++ runtime is baked into the binary and the device's 2011 libstdc++ is
never used. glibc stays dynamic, which is why the toolchain is built **against the
SDK's sysroot** rather than with a freshly built libc.

---

## 1. Host packages

**Debian / Ubuntu**

```bash
sudo apt install build-essential cmake git wget tar xz-utils perl \
                 pkg-config gperf coreutils gawk sed \
                 flex bison texinfo \
                 libssl-dev zlib1g-dev \
                 dpkg-dev debhelper fakeroot
```

**Fedora**

```bash
sudo dnf install gcc gcc-c++ make cmake git wget tar xz perl \
                 pkgconf-pkg-config gperf coreutils gawk sed \
                 flex bison texinfo \
                 openssl-devel zlib-devel \
                 dpkg-dev debhelper fakeroot
```

CMake must be **≥ 3.20**.

Two non-obvious entries:

- **`libssl-dev` / `zlib1g-dev` on the host.** Cross-building TDLib runs a *native*
  pass first (`prepare_cross_compiling`) that generates its `td_api` sources. That
  pass resolves OpenSSL and zlib against the host, so the headers are needed even
  though the shipped binary uses the cross-built copies.
- **`flex`, `bison`, `texinfo`** are only for `tools/build-toolchain.sh`.

**Do not install Qt4 on the host.** `debian/control` lists `libqt4-dev`, but that
is vestigial from the qmake era — `dh_shlibdeps` is commented out in `debian/rules`,
so build-deps are not enforced. Qt comes from the sysroot.

---

## 2. Submodules

A fresh clone cannot configure until these are present; CMake fails in
`cmake/qr_code_generator.cmake`.

```bash
git submodule update --init --recursive
```

---

## 3. Cross-toolchain

```bash
export QT_SDK_PATH=/path/to/QtSDK
./tools/build-toolchain.sh
```

Builds binutils + GCC into `~/cross/arm-harmattan`, targeting
`$QT_SDK_PATH/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim`. Then:

```bash
export PATH="$HOME/cross/arm-harmattan/bin:$PATH"
export TOOLCHAIN_PREFIX=arm-none-linux-gnueabi
```

Overridable: `PREFIX`, `TARGET`, `GCC_VERSION`, `BINUTILS_VERSION`, `ARM_ARCH`,
`ARM_FPU`, `FLOAT_ABI`, `WORKDIR`, `SYSROOT`.

The script ends by compiling a C++23 probe for ARM, so a successful run proves the
toolchain does what the project needs.

### Float ABI — hard, not softfp

Harmattan is **hard float**. The sysroot's libc reports:

```
$ readelf -A "$SYSROOT/lib/libc.so.6" | grep Tag_ABI_VFP_args
  Tag_ABI_VFP_args: VFP registers
```

meaning floating-point arguments are passed in VFP registers. Building softfp
against it fails at link with *"uses VFP register arguments, output does not"*.

This is set in two places, and **they must agree**:

| Where | Setting |
|---|---|
| `tools/build-toolchain.sh` | `FLOAT_ABI=hard` → `--with-float=hard` |
| `CMakeLists.txt` (`BUILD_HARMATTAN`) | `-mfloat-abi=hard` |

The toolchain's `--with-float` also makes everything `setup-dependencies.sh` builds
(zlib, OpenSSL, TDLib) inherit hard float automatically.

> Note: MeeGo 1.2 ARM used hard float, unlike Maemo 5 before it. N9-era recipes
> found online that specify softfp are for the wrong lineage — trust `readelf`.

### If GCC fails to build

The usual cause is GCC's libstdc++ using glibc functions the ~2011 sysroot lacks.
Retry with an older compiler; all of these handle the C++23 this project uses:

```bash
GCC_VERSION=13.3.0 ./tools/build-toolchain.sh
GCC_VERSION=12.4.0 ./tools/build-toolchain.sh
```

---

## 4. Dependencies

```bash
export TOOLCHAIN_PREFIX=arm-none-linux-gnueabi
./tools/setup-dependencies.sh harmattan "$QT_SDK_PATH"
```

Downloads, verifies and cross-builds **zlib 1.3.1**, **OpenSSL 3.3.1** and **TDLib**
(`MinSizeRel`, LTO), installing them into the sysroot.

**rlottie is not covered by this script.** `CMakeLists.txt` has
`find_package(rlottie REQUIRED)`, and there is no `Findrlottie.cmake` in-tree, so
you must cross-build and install it yourself and may need to pass `rlottie_DIR`.

TDLib is the heavy part — its generated sources want several GB of RAM. The script
uses `make -j4`; lower it if you hit OOM.

---

## 5. Configure and build

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=tools/toolchain.cmake \
  -DQT_SDK_PATH="$QT_SDK_PATH" \
  -DBUILD_HARMATTAN=ON

cmake --build build -j4
```

Confirm optimisation actually reaches the compiler — the project builds `Release`
by default now, but it is worth checking once:

```bash
cmake --build build -- VERBOSE=1 | grep -o '\-O[0-9s]' | sort -u    # expect -O2
```

### Build options

| Option | Default | Purpose |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | Defaulted explicitly; unset used to mean `-O0`. |
| `BUILD_HARMATTAN` | `OFF` | Device ABI flags, boostable, install rules, packaging. |
| `QT_SDK_PATH` | — | SDK root. Also feeds `QML_IMPORT_PATH` for the Qt Creator code model. |
| `MEEGRAM_GL_VIEWPORT` | `ON` | `QGLWidget` viewport for `QDeclarativeView`. Turn off to A/B against software paint. |
| `MEEGRAM_PROFILE` | `OFF` | Enables `src/ScopeTimer.hpp`; prints a timing table to stderr every 5 s. |

Run the app over SSH when profiling — `invoker` in the `.desktop` swallows stderr.

---

## 6. Package

```bash
cmake --build build --target package
```

Runs `mad dpkg-buildpackage -nc -uc -us` in the build directory. CMake copies
`debian/` there first, and `debian/rules` installs via
`cmake --build . --target install DESTDIR=debian/meegram`.

**`-nc` means no clean — packaging does not compile.** Always build first.

The `.deb` appears one level above the build directory. Install on device with
`dpkg -i`. Layout: binary → `/opt/meegram/bin`, desktop file →
`/usr/share/applications`, icon → `/usr/share/icons/hicolor/80x80/apps`.

---

## Known rough edges

**`tools/toolchain.cmake` has no sysroot.** It sets only the compiler names — no
`CMAKE_SYSROOT`, no `CMAKE_FIND_ROOT_PATH`. If your cross-GCC does not have the
sysroot baked in, `find_package(Qt4)` may match your *host* Qt and fail
confusingly. The complication is that the sysroot's `qmake` is an ARM binary and
cannot run on the host, so `FindQt4` needs a host-runnable qmake (MADDE supplies a
wrapper; a standalone toolchain needs `QT_QMAKE_EXECUTABLE` pointed somewhere
sensible).

**MADDE binaries are 32-bit x86.** On a 64-bit host:

```bash
sudo dpkg --add-architecture i386
sudo apt install libc6:i386 libstdc++6:i386 zlib1g:i386
```

**`debian/compat` is 7**, which modern debhelper treats as deprecated and may
refuse. Packaging normally runs inside MADDE's own environment, so host debhelper
may never be used — but if you see a compat error, bumping that file is the fix.

**A "slim" sysroot may lack development headers.** `tools/build-toolchain.sh`
checks for `usr/include/features.h` and stops early rather than failing deep into a
GCC build.

---

## Verification status

Everything above is derived from `CMakeLists.txt`, `tools/setup-dependencies.sh`,
`debian/rules` and the sysroot's own ELF attributes. The float ABI was confirmed by
`readelf` against a real Harmattan sysroot. The rest has **not** been validated end
to end in one pass — there is no CI in this repository and no recorded build
command, so treat the first run as a shakedown and expect to iterate, particularly
around rlottie and the sysroot question above.
