## Overview
`CPlayground` is a data-oriented C99 engine/editor project with a small amount of C++ used for basis texture compression. It builds an SDL3 GPU-based executable and now contains much more than a playground: a scene system, deferred renderer, asset pipeline, editor UI, animation, and streamed voxel terrain.

## Features
- Scene and asset pipeline with glTF/FBX import
- Compute shader Hi-Z oclussion culling + frustum culling for draws and lights.
- Texture system with 4k texture2d atlass array's instead of bindless textures
- All objects in a world can be rendered with single draw call
- Clay layout with SDF UI shapes such as rounded rectangle circle
- Slug text rendering high quality with all alphabets/languages supported without texture atlasses. Thanks Eric Lengyel
- Editor tooling with dockable UI, scene editing, asset browsing, and console output.
- Streamed voxel terrain with procedural generation, sculpt/paint editing, and persisted terrain edits.
- Custom runtime systems for animation, texture paging, memory allocation, async jobs, and SIMD-oriented math.

## Graphics Features
- Deferred lighting.
- Static surface, skinned mesh, and terrain pipelines.
- Directional, point, and spot shadows.
- Animation compute and animated-vertex generation.
- HBAO, MLAA, tonemapping and god rays.
- Precompiled shader outputs for SPIR-V and Metal (`spv/`, `msl/`).

## Project Layout
- `Source/Rendering/`: renderer, pipelines, compute passes, shadows, draw submission.
- `Source/AssetManagement/`: glTF/FBX import, mesh baking, texture processing, asset caching.
- `Source/Editor/`: editor windows, scene tools, terrain tools, asset browser, console.
- `Source/Terrain/`: streamed voxel terrain, Transvoxel meshing, edit persistence.
- `Source/UI/`: custom UI renderer/windowing/text integration.
- `Include/`: public engine headers.
- `Math/`: math types, matrices, vectors, colors, quaternions, SIMD helpers.
- `Extern/`: bundled third-party dependencies.

## External Libraries

| Library | Location | Purpose | Used by |
| --- | --- | --- | --- |
| SDL3 | `Extern/SDL3/` | Platform + GPU backend | `CMakeLists.txt` |
| basis_universal | `Extern/basis_universal/` | Texture compression | `CMakeLists.txt` |
| ufbx | `Extern/ufbx.c`, `Extern/ufbx.h` | FBX import | `Source/AssetManagement/AssetManager.c` |
| meshoptimizer | `Extern/meshoptimizer/` | Mesh optimization | `Extern/ExternAll.cpp`, `Source/AssetManagement/MeshBake.c` |
| clay | `Extern/clay/` | Editor UI layout | `Include/UIRenderer.h` |
| kb_text_shape | `Extern/kb/kb_text_shape.h` | Text shaping | `Source/UI/KBTextShape.c` |
| stb_rect_pack | `Extern/stb/stb_rect_pack.h` | Atlas packing | `Include/TextureSystem.h` |
| stb_sprintf | `Extern/stb/stb_sprintf.h` | Formatting | `Source/Platform.c` |
| stb_image | `Extern/stb/stb_image.h` | Image loading | tools/scripts |
| stb_image_resize2 | `Extern/stb/stb_image_resize2.h` | Image resizing | tools/scripts |
| stb_truetype | `Extern/stb/stb_truetype.h` | Font parsing | `Source/UI/Slug.c` |
| stb_image_write | `Extern/stb/stb_image_write.h` | Image writing | tools/scripts |
| stb_perlin | `Extern/stb/stb_perlin.h` | Terrain noise | graphics|
| tlsf | `Extern/tlsf.c`, `Extern/tlsf.h` | Allocator | `Source/Memory.c`, `Source/Rendering/Graphics.c` |
| dynarray | `Extern/dynarray.c`, `Extern/dynarray.h` | Dynamic arrays | `Source/AssetManagement/AssetManager.c` |
| sj | `Extern/sj.h` | glTF JSON parsing | `Source/AssetManagement/GLTFParser.c` |
| sdefl / sinfl | `Extern/sdefl.h`, `Extern/sinfl.h` | Compression | `Source/AssetManagement/AssetManager.c`, `Source/Terrain/TerrainEdit.c` |

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
