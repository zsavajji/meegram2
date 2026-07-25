---
layout: home

hero:
  name: MeeGram
  text: Telegram for the Nokia N9
  tagline: An unofficial TDLib client for MeeGo 1.2 Harmattan — Qt 4.7.4, QtDeclarative, one 2011 Cortex-A8.
  actions:
    - theme: brand
      text: Build it
      link: /building
    - theme: alt
      text: What it does
      link: /features
    - theme: alt
      text: How it works
      link: /architecture
    - theme: alt
      text: GitHub
      link: https://github.com/qtinsider/meegram2

features:
  - title: Features
    details: Every feature that exists, what each one actually does on the device, and an honest list of what is not implemented.
    link: /features
    linkText: See the list
  - title: Architecture
    details: How the TDLib update stream fans out to four subscribers, what StorageManager caches, and why AppManager is the only door into the object graph from QML.
    link: /architecture
    linkText: Read the map
  - title: Building
    details: Host packages, the cross-toolchain you have to build yourself, dependencies, and packaging into a .deb for the device.
    link: /building
    linkText: Start to finish
  - title: Troubleshooting
    details: The errors this pipeline actually produces, what each one means, and the rough edges that are known but not yet fixed.
    link: /troubleshooting
    linkText: When it breaks
---

## Before you start

The Qt SDK ships **GCC 4.4.1**. This codebase is **C++23** — `std::jthread`,
`std::ranges`, `std::to_array`. Those are irreconcilable, and no compiler flag
bridges them.

You need a modern `arm-none-linux-gnueabi` GCC built against the Harmattan sysroot.
`tools/build-toolchain.sh` builds one, and that is the single largest obstacle
between a clean checkout and a running binary. Everything after it is mechanical.

## Target hardware

| | |
|---|---|
| Device | Nokia N9 (OMAP3630) |
| CPU | 1 GHz Cortex-A8, **hard-float** ABI |
| GPU | PowerVR SGX530 |
| Memory | 1 GB |
| Display | 854 × 480 |
| OS | MeeGo 1.2 Harmattan |
| Qt | 4.7.4 with QtDeclarative (QML1) |

QML1 shapes the whole front end: no scene graph, no per-row role cache in views,
and `QDeclarativeView` is a `QGraphicsView` — so rendering is CPU rasterisation
unless a GL viewport is installed.

## Honesty note

These docs are derived from the source, the build scripts and the sysroot's own ELF
attributes. Two things are confirmed against real artefacts: the **float ABI**
(`readelf` on the sysroot's libc) and **TDLib API compatibility** (every symbol and
field the code uses, checked against TDLib 1.8.66).

The pipeline as a whole has **not** been run start to finish in one clean pass, and
there is no CI in the repository. Treat the first build as a shakedown.
