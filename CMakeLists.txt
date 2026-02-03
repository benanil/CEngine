cmake_minimum_required(VERSION 3.16)

# set the output directory for built objects.
# This makes sure that the dynamic library goes into the build directory automatically.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

# prevent installing to system directories. 
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE INTERNAL "")

set(CMAKE_MSVC_PARALLEL_COMPILE OFF)

# Declare the project
project(CPlayground)

if ((APPLE AND NOT CMAKE_SYSTEM_NAME MATCHES "Darwin") OR EMSCRIPTEN)
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")    # Disable shared builds on platforms where it does not make sense to use them
    set(SDL_SHARED OFF)
else()
    set(SDL_SHARED ON)
endif()

# if(MSVC)
#     if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
#         add_definitions(/MP)				# parallelize each target, unless Ninja is the generator
#     endif()
# endif()

# Set the name of the executable
set(EXECUTABLE_NAME ${PROJECT_NAME})

include_directories(${CMAKE_SOURCE_DIR})

add_executable(${EXECUTABLE_NAME})

find_package(OpenMP)
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

# Add your sources to the target
target_sources(${EXECUTABLE_NAME} PRIVATE
Source/Main.c
Source/OS.c
Extern/dynarray.c 
Source/TLSF.c
Source/Memory.c
Source/ECS.c
Source/FileSystem.c
Source/Platform.c 
Source/Graphics.c 
Source/GLTFParser.c 
Source/Animation.c
Source/Algorithm.c
Source/AssetManager.c
Source/BasisSokol.cpp 
Extern/ExternAll.cpp
)

# Configure SDL by calling its CMake file.
# we use EXCLUDE_FROM_ALL so that its install targets and configs don't
# pollute upwards into our configuration.
add_subdirectory(Extern/SDL3 EXCLUDE_FROM_ALL)

# Link SDL to our executable. This also makes its include directory available to us. 
target_link_libraries(${EXECUTABLE_NAME} PUBLIC SDL3::SDL3) # If using satelite libraries, SDL must be the last item in the list. 

set_property(TARGET CPlayground PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
# on Visual Studio, set our app as the default project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "${EXECUTABLE_NAME}")
