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

### Windows (Visual Studio Generator)
```
cmake -S . -B build
cmake --build build --config Debug
cmake --build build --config Release
```

### Windows (Ninja)
```
winget install Ninja-build.Ninja
cmake -S . -B Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build Debug
cmake -S . -B Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Release
```

### macOS (Unix Makefiles)
```
cmake -S . -B build
cmake --build build
```

### Linux (Unix Makefiles)
```
cmake -S . -B build
cmake --build build
```
