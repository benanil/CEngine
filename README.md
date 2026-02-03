## Overview
CPlayground is a C/C++ playground project that builds an SDL-based executable. This repository includes engine-style modules (e.g., ECS, memory, assets, graphics), shaders, and third-party dependencies bundled under `Extern/`.

## Prerequisites
Install a C/C++ toolchain and CMake 3.16+.

### Windows
- Visual Studio (MSVC) or LLVM/Clang
- Optional: Ninja for faster builds

### macOS
- Xcode Command Line Tools
- CMake (via Homebrew or a standalone installer)

### Linux
- GCC or Clang
- CMake (from your distro package manager)
- SDL3 dependencies (if your distro packages SDL3, install it; otherwise the bundled SDL3 submodule is used)

## How to Compile

```
cmake -Bbuild
cmake --build build
```

### Ninja (Windows example)
```
winget install Ninja-build.Ninja
cmake -S . -B Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build Debug
cmake -S . -B Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Release
```
