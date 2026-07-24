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
