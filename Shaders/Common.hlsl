#pragma once
#ifndef HLSL_COMMON_H
#define HLSL_COMMON_H
#pragma dxc enable_16bit_types

#if defined(VULKAN)
    #define PLATFORM_VULKAN 1
#else
    #define PLATFORM_VULKAN 0
#endif

#ifndef FLOAT16_SUPPORTED
    #if PLATFORM_VULKAN
        #define FLOAT16_SUPPORTED 1
    #elif defined(__SHADER_TARGET_MAJOR) && (__SHADER_TARGET_MAJOR >= 6) && (__SHADER_TARGET_MINOR >= 2)
        #define FLOAT16_SUPPORTED 1
    #else
        #define FLOAT16_SUPPORTED 0
    #endif
#endif

#ifndef INT16_SUPPORTED
    #if PLATFORM_VULKAN
        #define INT16_SUPPORTED 1
    #elif defined(__SHADER_TARGET_MAJOR) && (__SHADER_TARGET_MAJOR >= 6) && (__SHADER_TARGET_MINOR >= 2)
        #define INT16_SUPPORTED 1
    #else
        #define INT16_SUPPORTED 0
    #endif
#endif

// these are supported on mobile and 9070xt and above on amd, but not nvidia rtx5000 series
#define FLOAT16_IO_SUPPORTED 0
#define INT16_IO_SUPPORTED   0

#define USE_16BIT_TYPES (INT16_SUPPORTED && FLOAT16_SUPPORTED)

#ifdef __10X__ 
    #define out
    #define inout
#endif

#if INT16_SUPPORTED
    typedef int16_t   s16;
    typedef uint16_t  u16;
    typedef int16_t2  s16x2;
    typedef int16_t3  s16x3;
    typedef int16_t4  s16x4;
    typedef uint16_t2 u16x2;
    typedef uint16_t3 u16x3;
    typedef uint16_t4 u16x4;
#else
    typedef int   s16;
    typedef uint  u16;
    typedef int2  s16x2;
    typedef int3  s16x3;
    typedef int4  s16x4;
    typedef uint2 u16x2;
    typedef uint3 u16x3;
    typedef uint4 u16x4;
#endif

#if FLOAT16_SUPPORTED && !PLATFORM_VULKAN
    typedef float16_t    fp16;
    typedef float16_t2   fp16_2;
    typedef float16_t3   fp16_3;
    typedef float16_t4   fp16_4;
    typedef float16_t3x3 fp16_3x3;
    typedef float16_t3x4 fp16_3x4;
    typedef float16_t4x4 fp16_4x4;
    typedef float16_t4x3 fp16_4x3;
#elif FLOAT16_SUPPORTED
    typedef half    fp16;
    typedef half2   fp16_2;
    typedef half3   fp16_3;
    typedef half4   fp16_4;
    typedef half3x3 fp16_3x3;
    typedef half3x4 fp16_3x4;
    typedef half4x4 fp16_4x4;
    typedef half4x3 fp16_4x3;
#elif !PLATFORM_VULKAN && !defined(__DXC_VERSION_MAJOR)
    typedef min16float    fp16;
    typedef min16float2   fp16_2;
    typedef min16float3   fp16_3;
    typedef min16float4   fp16_4;
    typedef min16float3x3 fp16_3x3;
    typedef min16float3x4 fp16_3x4;
    typedef min16float4x4 fp16_4x4;
    typedef min16float4x3 fp16_4x3;
#else
    typedef float    fp16;
    typedef float2   fp16_2;
    typedef float3   fp16_3;
    typedef float4   fp16_4;
    typedef float3x3 fp16_3x3;
    typedef float3x4 fp16_3x4;
    typedef float4x4 fp16_4x4;
    typedef float4x3 fp16_4x3;
#endif

#if FLOAT16_IO_SUPPORTED
    typedef fp16   fp16_io;
    typedef fp16_2 fp16_2_io;
    typedef fp16_3 fp16_3_io;
    typedef fp16_4 fp16_4_io;
#else
    typedef float  fp16_io;
    typedef float2 fp16_2_io;
    typedef float3 fp16_3_io;
    typedef float4 fp16_4_io;
#endif

#if INT16_IO_SUPPORTED
    typedef s16   s16_io;
    typedef u16   u16_io;
    typedef s16x2 s16x2_io;
    typedef u16x2 u16x2_io;
    typedef s16x4 s16x4_io;
    typedef u16x4 u16x4_io;
#else
    typedef int   s16_io;
    typedef uint  u16_io;
    typedef int2  s16x2_io;
    typedef uint2 u16x2_io;
    typedef int4  s16x4_io;
    typedef uint4 u16x4_io;
#endif

#endif // HLSL_COMMON_H