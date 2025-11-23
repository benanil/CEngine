cmake_minimum_required(VERSION 3.20)
project(Main C CXX)  # Add CXX since you have .cpp files

if (MSVC)
    add_compile_options(
        /arch:AVX2
        /GR-
        /GS-
        /EHsc-
    )
    
    # Force C99/C11 standard for C files
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()

set(C_SOURCES
    Main.c
    OS.c
    Extern/dynarray.c
    TLSF.c
    Memory.c
    FileSystem.c
    Platform.c
    Graphics.c
    GLTFParser.c
    Animation.c
    Algorithm.c
    AssetManager.c
    Extern/ufbx.c
)

set(CPP_SOURCES
    BasisSokol.cpp
)

add_executable(MainMSVC ${C_SOURCES} ${CPP_SOURCES})

# Explicitly set C files as C (not C++)
set_source_files_properties(${C_SOURCES} PROPERTIES LANGUAGE C)

# Explicitly set C++ files as C++
set_source_files_properties(${CPP_SOURCES} PROPERTIES LANGUAGE CXX)

# Add this to help IntelliSense
set_target_properties(MainMSVC PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	VS_GLOBAL_VcpkgEnabled false
)

# If you have include directories, add them explicitly
# target_include_directories(MainMSVC PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(MainMSVC PRIVATE
    advapi32
    d3d11
    dxgi
    dxguid
    user32
    shell32
    gdi32
    winmm
)