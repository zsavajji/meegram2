# Building MeeGram

MeeGram targets the **Nokia N9** running MeeGo 1.2 Harmattan: 1 GHz Cortex-A8
(OMAP3630), PowerVR SGX530, 1 GB RAM, 854×480, **Qt 4.7.4 with QtDeclarative
(QML1)**. It is built with CMake and cross-compiled from Linux; Qt Creator is
useful for editing and for deploying to the device, but it does not drive the build.

---

## The whole thing, start to finish

```bash
# 0. host packages - see section 1
git submodule update --init --recursive

# 1. cross-toolchain (long; only needed once)
export QT_SDK_PATH=/path/to/QtSDK
./tools/build-toolchain.sh

export PATH="$HOME/cross/arm-harmattan/bin:$PATH"
export TOOLCHAIN_PREFIX=arm-none-linux-gnueabi

# 2. dependencies - note BOTH arguments are required
./tools/setup-dependencies.sh harmattan "$QT_SDK_PATH"

# 3. build
cmake -B build-app \
  -DCMAKE_TOOLCHAIN_FILE=tools/toolchain.cmake \
  -DQT_SDK_PATH="$QT_SDK_PATH" \
  -DBUILD_HARMATTAN=ON
cmake --build build-app -j4

# 4. package
cmake --build build-app --target package
```

Everything below explains a step or a failure mode.

---

## The one thing that will block you

The Qt SDK's bundled MADDE toolchain is **GCC 4.4.1**, which predates C++11.
This codebase is **C++23** (`CMAKE_CXX_STANDARD 23`) and uses `std::jthread`
(`src/Client.cpp`), `std::ranges`, and `std::to_array` (`src/Emoji.cpp`).

::: danger You cannot build MeeGram with the stock SDK compiler
No combination of flags changes this. You need a modern `arm-none-linux-gnueabi`
GCC targeting the Harmattan sysroot — `tools/build-toolchain.sh` builds one.
:::

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

::: warning Do not install Qt4 on the host
`debian/control` lists `libqt4-dev`, but that is vestigial from the qmake era —
`dh_shlibdeps` is commented out in `debian/rules`, so build-deps are not enforced.
Qt comes from the sysroot.
:::

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

Builds **binutils 2.42** + **GCC 14.2.0** into `~/cross/arm-harmattan`, targeting
`$QT_SDK_PATH/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim`. Then:

```bash
export PATH="$HOME/cross/arm-harmattan/bin:$PATH"
export TOOLCHAIN_PREFIX=arm-none-linux-gnueabi
```

Overridable: `PREFIX`, `TARGET`, `GCC_VERSION`, `BINUTILS_VERSION`, `ARM_ARCH`,
`ARM_FPU`, `FLOAT_ABI`, `WORKDIR`, `SYSROOT`, `FORCE_REBUILD`.

**Re-running is cheap.** A stage whose output already exists in `$PREFIX` is
skipped, so a second run only re-verifies. `FORCE_REBUILD=1` rebuilds regardless.

The script ends by compiling a probe for ARM that exercises `std::to_array`,
`std::ranges` and **`std::jthread`** — the last one matters, because it needs
libstdc++ built with thread support, which is the usual casualty of a misconfigured
cross build and only shows up at link time. The probe is compiled and linked, not
run (it is an ARM binary), and the script asserts `file` reports it as ARM.

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

> MeeGo 1.2 ARM used hard float, unlike Maemo 5 before it. N9-era recipes found
> online that specify softfp are for the wrong lineage — trust `readelf`.

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

::: warning Both arguments are required
The first is the target (`harmattan` or `simulator`); the second is the Qt SDK root.
:::

The script validates before building: `TOOLCHAIN_PREFIX` is set, `$TOOLCHAIN_PREFIX-gcc` is on `PATH`, an SDK path was
given, and the sysroot exists.

Cross-builds and installs into the sysroot:

| Dependency | Version | Pinning |
|---|---|---|
| zlib | 1.3.2 | version + SHA256 |
| OpenSSL | 3.5.7 | version + SHA256 |
| rlottie | master | **commit SHA** (`RLOTTIE_COMMIT`) |
| TDLib | 1.8.66 | **commit SHA** (`TDLIB_COMMIT`) |

TDLib tags almost nothing — its git tags stop at `v1.8.0` while it reports versions
like 1.8.66 — so a commit is the only way to pin it. This used to be
`git clone --depth=1` of master, meaning no two builds were guaranteed to use the
same TDLib. Override with `TDLIB_COMMIT=<sha>`; bump deliberately, after verifying
a build runs on the device.

OpenSSL 3.5 is the current **LTS** branch (supported to 2030-04-08). The previous
pin, 3.3.1, sat on a branch that went end-of-support on 2026-04-09.

rlottie is built static, with `LOTTIE_MODULE` and `LOTTIE_THREAD` off:
`LottieAnimation` calls `renderSync()` on the GUI thread and the N9 is single-core,
so render threads cost memory and buy nothing. `CMAKE_POSITION_INDEPENDENT_CODE` is
on because `meegram` links `-pie`, and a non-PIC static archive will not link into a
PIE executable on ARM.

### What a re-run does

| Stage | Re-run |
|---|---|
| zlib, OpenSSL | **Skipped** if `build/*/.built-version` matches the version in the script |
| rlottie, TDLib | **Skipped** if `build/*/.built-commit` matches the pinned commit, target and LTO setting. The stamp is only written after `install` succeeds, so a failed run always rebuilds. |

Stamps mean bumping a version or commit still triggers a rebuild rather than
silently linking against a stale library. `FORCE_REBUILD=1` forces all of them.
When a rebuild does happen the build directory is wiped first: a stale CMake cache
in a cross-build is a good way to get a subtly wrong binary.

### Parallelism

All three dependencies build with `-j$JOBS`, default **4**:

```bash
JOBS=8 ./tools/setup-dependencies.sh harmattan "$QT_SDK_PATH"   # more cores
JOBS=2 ./tools/setup-dependencies.sh harmattan "$QT_SDK_PATH"   # less RAM
```

TDLib previously built **fully serial** — `cmake --build` defaults to one job with
the Makefile generator, so the longest build got one core while zlib got four.

::: warning Parallelism costs RAM more than CPU here
TDLib's generated `td_api` translation units routinely want a gigabyte or more
each, and `TD_ENABLE_LTO=ON` adds link-time pressure on top. `JOBS=4` is
comfortable on 8 GB. If the build is OOM-killed — usually a bare `Killed` message
partway through — drop to `JOBS=2`.
:::

---

## 5. Configure and build

```bash
cmake -B build-app \
  -DCMAKE_TOOLCHAIN_FILE=tools/toolchain.cmake \
  -DQT_SDK_PATH="$QT_SDK_PATH" \
  -DBUILD_HARMATTAN=ON

cmake --build build-app -j4
```

Confirm optimisation actually reaches the compiler — the project builds `Release`
by default now, but it is worth checking once:

```bash
cmake --build build-app -- VERBOSE=1 | grep -o '\-O[0-9s]' | sort -u    # expect -O2
```

::: danger Do not build the app into `build/`
`tools/setup-dependencies.sh` owns that directory — `build/crypto`, `build/zlib`,
`build/tdlib`, `build/generate` and `build/rlottie` all live there, along with the
stamps that let re-runs skip completed work. Pointing CMake at `build/` and then
clearing the cache with `rm -rf build` destroys every dependency build tree.
Installed artefacts in the sysroot survive, but the trees and stamps do not.
:::

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
cmake --build build-app --target package
```

Runs `mad dpkg-buildpackage -nc -uc -us` in the build directory. CMake copies
`debian/` there first, and `debian/rules` installs via
`cmake --build . --target install DESTDIR=debian/meegram`.

::: warning `-nc` means no clean — packaging does not compile
Always build first. `dpkg-buildpackage` only wraps what is already in the build
directory.
:::

The `.deb` appears one level above the build directory. Install on device with
`dpkg -i`. Layout: binary → `/opt/meegram/bin`, desktop file →
`/usr/share/applications`, icon → `/usr/share/icons/hicolor/80x80/apps`.

---
