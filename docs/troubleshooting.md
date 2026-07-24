# Troubleshooting

Errors this pipeline actually produces, what each means, and the rough edges
that are known but not yet fixed.

## Build errors

**`No device specified. Please specify 'harmattan' or 'simulator'.`**
`setup-dependencies.sh` takes the target as its first argument and the SDK root as
its second: `./tools/setup-dependencies.sh harmattan /path/to/QtSDK`.

**`TOOLCHAIN_PREFIX is not set` / `<prefix>-gcc is not on PATH`**
Export both before running:
`export PATH="$HOME/cross/arm-harmattan/bin:$PATH"` and
`export TOOLCHAIN_PREFIX=arm-none-linux-gnueabi`.

**`No SDK path given`**
Without it TDLib's install step would target `/Madde/...` at the filesystem root —
and it is only read at the very end, after the whole build. Hence the up-front check.

**`Found relative path while evaluating include directories` (CMake, TDLib)**
Fixed. The CMake options were built as a string containing escaped quotes and then
expanded unquoted, so CMake received `-DOPENSSL_INCLUDE_DIR="/path"` with the quotes
inside the value. They are bash arrays now. If you see this, your `tools/` is stale.

**`Compatibility with CMake < 3.5 has been removed from CMake` (rlottie)**
rlottie still declares `cmake_minimum_required(VERSION 3.3)` and CMake 4.x dropped
support below 3.5. The script passes `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`, which is
CMake's own documented escape hatch and also covers rlottie's bundled
freetype/pixman/stb subprojects. TDLib declares 3.10 and is unaffected. If you see
this, your `tools/` is stale.

**`Found neither 'TD_HAS_MMSG 1' nor 'TD_HAS_MMSG 0'`**
The `td/` checkout is reused across runs, so the patch has to be idempotent — this
error now means the macro genuinely moved upstream, not that it was already applied.
Check `td/tdutils/td/utils/port/config.h`. The patch disables `recvmmsg`/`sendmmsg`,
which the N9's kernel does not have.

**`Compiler exited 0 but produced no binary` / `Probe is not an ARM binary`**
The toolchain verification refuses to report success without a real ARM binary.
Usually means the wrong compiler is first on `PATH`.

---

**`mad: No '-t <target>' option given nor default target set`**
`CMakeLists.txt` calls `mad` without `-t`. Run `mad list`, then
`mad set <target>` once — the name matches a directory under `Madde/targets/`.

**`'class td::td_api::chatFolderInfo' has no member named 'title_'`**
TDLib API drift. `chatFolderInfo` used to carry `title:string`; it now holds
`name:chatFolderName`, which wraps a `formattedText`, so the title sits two levels
down. This class of break is worth expecting after any `TDLIB_COMMIT` bump — the
fix is always to read the current `td_api.tl` for the type in question.

**`libcrypto.so.3: cannot open shared object file` on the device**
The package is missing `/opt/meegram/lib`, or the binary lost its `RPATH`. Check
both with the two commands in [Verify before touching the
device](/building#verify-before-touching-the-device).

### Warnings that are safe to ignore

**`.dynsym local symbol at index N (>= sh_info of 3)`** on `libQtGui.so` /
`libQtOpenGL.so` — binutils 2.42 being strict about a malformed dynamic symbol
table in 2011-era Qt libraries. Unfixable short of rebuilding Qt, and harmless: the
device's own loader reads them fine.

**`function may return address of local variable`** in `sqlite3.c` — a long-standing
false positive in SQLite's `sqlite3SelectNew`, which copies the contents out before
returning. TDLib bundles SQLite unmodified.

**`Cannot generate a safe runtime search path … libz.so.1 … may be hidden by
build/zlib/lib`** — concerns the build-tree binary only. `INSTALL_RPATH` reduces the
installed binary's rpath to `/opt/meegram/lib`, so on the device `libz.so.1`
resolves from the platform. zlib's ABI is stable across 1.2.x/1.3.x. The underlying
cause is that `setup-dependencies.sh` leaves zlib and OpenSSL in `build/` rather
than installing them into the sysroot.

**`Manually-specified variables were not used: CMAKE_TOOLCHAIN_FILE`** — appears when
re-configuring an existing build directory. The toolchain file is read on the first
configure and stored in the cache; passing it again is redundant, not ignored.
Confirm with `grep cxx_compiler build-app/CMakeCache.txt`.

**changelog parse warnings / `unknown substitution variable ${shlibs:Depends}`** —
`debian/changelog` is missing blank-line separators between entries, and
`dh_shlibdeps` is commented out in `debian/rules`, so the package declares no
dependencies. Neither affects a hand-installed `.deb`.

## Known rough edges

**Qt4 not found** — `Found unsuitable Qt version "" from NOTFOUND`.
`FindQt4` locates Qt by *running* qmake, and the sysroot's qmake is an ARM binary
that cannot execute on the build host. `tools/toolchain.cmake` resolves this by
globbing MADDE's host qmake at `$QT_SDK_PATH/Madde/targets/harmattan*/bin/qmake`,
which conveniently already reports sysroot-prefixed paths. If your SDK lays that
out differently, pass `-DQT_QMAKE_EXECUTABLE=` explicitly; verify with
`file <qmake>` that it is an x86 binary, not ARM.

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

Confirmed against real artefacts:

- **Float ABI** — `readelf -A` on a real Harmattan sysroot's libc.
- **TDLib API compatibility** — all 221 `td_api` types/methods and all 132 distinct
  field accesses in `src/` exist in TDLib 1.8.66. Several signatures did change
  underneath (`sendMessage` gained `topic_id:MessageTopic`, `inputMessageText`
  replaced `disable_web_page_preview` with `link_preview_options`), but only in
  fields this code never sets. The one genuinely stale call site,
  `inputMessageReplyToMessage` in `src/MessageModel.cpp`, is commented out and will
  need rewriting for the current signature if reply support is revived.

::: info Not yet validated end to end
 there is no CI in this repository, and the pipeline
has not been run start-to-finish in a single clean pass. rlottie and the sysroot question above are the two places most likely to need
iteration.
:::
