## Overview
`CPlayground` is a data-oriented C99 engine/editor project with a small amount of C++ used for bundled dependency integration. It builds an SDL3 GPU-based executable and now contains much more than a playground: a scene system, deferred renderer, asset pipeline, editor UI, animation, and streamed voxel terrain.

## Features
- SDL3 GPU renderer with a deferred lighting path, compute-driven passes, and platform abstraction through SDL.
- Scene system with resident bundle caching, active scene switching, static and skinned render sets, per-scene texture atlases, and per-scene animation state.
- glTF runtime/import pipeline plus FBX import, binary bundle caching, mesh baking, texture baking, and scene serialization.
- GPU skinned animation pipeline with baked animation data, compute-updated bone buffers, and animated vertex generation.
- Compute-assisted visibility and post-processing, including Hi-Z generation, occlusion culling, local light culling, HBAO, MLAA, deferred lighting, and tonemapping.
- Shadowing support for cascaded directional shadows plus point and spot shadow maps.
- Streamed voxel terrain using the Transvoxel algorithm, worker-thread chunk generation, procedural terrain parameters, sculpt/paint editing, and persisted edit chunks.
- Scene and terrain raycasting for editor picking and interaction.
- Editor UI with dockable/persistent windows, scene view, settings panels, asset browser, scene editor, terrain editor, and console logging.
- Texture system with paged array atlases, descriptor/material tables, compressed texture handling, and baked atlas restore/save paths.
- Custom memory/runtime systems including TLSF-backed persistent allocation, arena usage, async worker jobs, SIMD-oriented math, and custom containers.

## External Libraries

### Integrated Into The Engine Build
| Library | Location | Used For | Notes |
| --- | --- | --- | --- |
| SDL3 | `Extern/SDL3/` | Windowing, input, audio init, threads, file/platform services, and `SDL_gpu` rendering backend | Added through `add_subdirectory()` in `CMakeLists.txt`. |
| basis_universal | `Extern/basis_universal/` | Texture compression/transcoding for baked texture pages and texture pipeline work | Multiple encoder/transcoder sources are compiled directly into the main target. |
| BasisCompressWrapper | `Extern/BasisCompressWrapper.*` | Local wrapper around Basis Universal | Bridges engine code to basis compression/transcoding entry points. |
| ufbx | `Extern/ufbx.c`, `Extern/ufbx.h` | FBX import | Compiled directly into the executable. |
| meshoptimizer | `Extern/meshoptimizer/` | Mesh optimization and simplification during asset baking | Pulled in through `Extern/ExternAll.cpp` and `Source/AssetManagement/MeshBake.c`. |
| clay | `Extern/clay/` | Immediate-mode layout/UI foundation for the editor | Included by `Include/UIRenderer.h`. |
| kb_text_shape | `Extern/kb/kb_text_shape.h` | Text shaping for the custom UI/text rendering path | Built via `Source/UI/KBTextShape.c`. |
| stb_rect_pack | `Extern/stb/stb_rect_pack.h` | Texture atlas packing | Used by `TextureSystem`. |
| stb_sprintf | `Extern/stb/stb_sprintf.h` | Lightweight formatting helpers | Used by `Platform.c`. |
| tlsf | `Extern/tlsf.c`, `Extern/tlsf.h` | Two-level segregated fit allocator for persistent/large engine allocations | Used by the global memory system and geometry heaps. |
| dynarray | `Extern/dynarray.c`, `Extern/dynarray.h` | Dynamic arrays in the asset pipeline | Used mainly by FBX/import code. |
| sj | `Extern/sj.h` | JSON reader for glTF parsing | Used by `GLTFParser.c`. |
| sdefl / sinfl | `Extern/sdefl.h`, `Extern/sinfl.h` | Compression/decompression for cached asset data and terrain edit chunks | Used by asset cache serialization and terrain edit persistence. |

### Vendored In `Extern/` But Not Obviously Wired Into The Main Target
| Library/File | Location | Notes |
| --- | --- | --- |
| xxHash | `Extern/xxhash.h` | Present in the repo, but not referenced by the current main target sources. |
| c89atomic | `Extern/c89atomic.h` | Vendored header, not obviously used by the current build. |
| miniperf | `Extern/miniperf.h` | Vendored header, not obviously used by the current build. |
| adler32 | `Extern/adler32.h` | Vendored helper header, not directly referenced by current engine sources. |
| stb repository | `Extern/stb/` | The repo contains the broader stb collection, while the engine currently uses `stb_rect_pack` and `stb_sprintf` directly. |

## Project Layout
- `Source/Rendering/`: renderer, pipelines, compute passes, shadows, draw submission.
- `Source/AssetManagement/`: glTF/FBX import, mesh baking, texture processing, asset caching.
- `Source/Editor/`: editor windows, scene tools, terrain tools, asset browser, console.
- `Source/Terrain/`: streamed voxel terrain, Transvoxel meshing, edit persistence.
- `Source/UI/`: custom UI renderer/windowing/text integration.
- `Include/`: public engine headers.
- `Math/`: math types, matrices, vectors, colors, quaternions, SIMD helpers.
- `Extern/`: bundled third-party dependencies.

## Prerequisites
Install a C/C++ toolchain and CMake 3.16+.

### Windows
- Visual Studio (MSVC) or LLVM/Clang
- Optional: Ninja for faster builds

### macOS
- Xcode Command Line Tools
- CMake

### Linux
- GCC or Clang
- CMake

## Build Notes
- SDL3 is bundled in `Extern/SDL3`, so no separate SDL install is normally required.
- OpenMP is detected optionally by CMake and enabled when found.
- The project builds a single executable target: `CPlayground`.
- Shader helper edits may require a forced rebuild, especially shared shader helper headers.

## How to Compile

### Basic build
```bash
cmake -S . -B build
cmake --build build
```

### Windows (Ninja)
```bash
winget install Ninja-build.Ninja
cmake -S . -B Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build Debug
cmake -S . -B Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Release
```
