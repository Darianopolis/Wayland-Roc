# Wayland Roc

An experiment in writing a simple opinionated independent Wayland compositor.

## Goals

 - Independence from Wayland frameworks / libraries
    - With an exception for libwayland implementing the Wayland wire protocol
 - Modern Vulkan based Wayland compositor
    - Consolidating on a single backend API and modern kernel version simplifies GPU allocation, buffer sharing and synchronization code paths
 - Move common desktop functionality into the compositor process via a plugin/applet system
    - Removes overhead of cross-process buffer sharing and synchronization
    - E.g.
        - Desktop background (with support for internally compressed formats)
        - System tray
        - Notification daemon
        - Output management (with support for screen size/refresh spoofing)
        - Screenshare / screenshot / screenrecord / color picking
        - Clipboard management
 - Universal input device remapping
    - Keyboard, mouse, touchpad, drawing tablet, foot pedal, gamepad, joystick, etc..
    - With support for routing global shortcut combinations to particular applications
    - Compositor-aware input mapping (e.g. per-window rules)

## Non-Goals

 - Support a wide range of (especially older) hardware
    - Currently focused on a modern RADV driver stack for simplicity
 - Support every Wayland protocol
    - Some Wayland protocol functionality can be replaced with plugin based functionality

# Building

#### System dependencies (build-time)

- python 3
- cmake
- ninja
- wayland-protocols
- xkbcommon
- mold
- clang (C++26 capable version at minimum)

#### Quickstart

```
$ python build.py -BI                           build + install
# setcap cap_sys_nice+ep ~/.local/bin/wroc      give NICE capability for global queue priority
```

#### Build Options

- `-B` : Build project
- `-I` : Install project
- `-R` : Build in release mode
- `-C` : Force reconfigure and clean build
- `-U` : Check and update dependencies (updates `build.json`)
- `--asan` : Enable address sanitizer
- `--system-slangc` : Look for `slangc` on path

#### Leak Sanitizer suppression

When building and running with the `--asan` option, certain libraries / drivers may leak memory unavoidably.
To suppress:

1) identify the shared objects leaking memory
2) Create a file `lsan.supp`
3) Add `leak:SOURCE` entries for each such shared object. E.g. `leak:libdbus-1.so`
4) Set the `LSAN_OPTIONS=suppressions=/path/to/lsan.supp` environmental variable when running
