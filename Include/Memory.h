#ifndef MEMORY_INCLUDED
#define MEMORY_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include "TLSF.h"

#ifndef TLSF_MEMORY_SIZE
    // 512 mb
    #define TLSF_MEMORY_SIZE (512 * 1000 * 1000)
#endif

#ifndef ARENA_MEMORY_SIZE
    // 32 mb
    #define ARENA_MEMORY_SIZE (32 * 1024 * 1024)
#endif

#define DEFER(_end_) for(int (_defer_##__LINE__) = 0; (_defer_##__LINE__) < 1; (_defer_##__LINE__)++, _end_)

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct Arena_ {
	char *buf;
	size_t         buffLen;
	size_t         currOffset;
} Arena;

typedef struct FixedFragment_
{
    struct FixedFragment_* next;
    char* ptr;
    size_t size; // used like an index until we fill the fragment (in bytes)
} FixedFragment;

typedef struct FixedPow2Allocator_
{
    size_t currentCapacity;   // capacity in bytes
    FixedFragment* base;
    FixedFragment* current;
} FixedPow2Allocator;


extern tlsf_t GlobalTLSF;
extern Arena GlobalArena;

// Alignment
uint64_t AlignAddress(uint64_t addr, uint64_t align);
void*    AlignPointer(void* ptr, uint64_t align);
void*    AllocAligned(uint64_t bytes, uint64_t align); // 'align' must be a power of 2 (typically 4, 8 or 16).
void     FreeAligned(void* pMem);

// Arena allocator
void     ArenaInit(Arena *a, size_t backing_buffer_length);
void*    ArenaAllocAlign(Arena *a, size_t size, size_t align);
void*    ArenaAlloc(Arena *a, size_t size);
void*    ArenaPushGlobal(uint64_t size);
void     ArenaPopGlobal(uint64_t size);
uint64_t ArenaRemainingCurrent();
uint64_t ArenaGetCurrentOfset();
void     ArenaSetCurrentOfset(size_t offset);

#define ArenaStruct(arena, type)      (ArenaAllocAlign(arena, sizeof(type), ALIGNOF(type)))
#define ArenaArray(arena, type, cnt)  (ArenaAllocAlign(arena, sizeof(type) * cnt, ALIGNOF(type)))
#define AlignPointer(ptr, align)(void*)AlignAddress((uint64_t)ptr, align);
#define ArenaAllocGlobal(cnt)         (ArenaAllocAlign(&GlobalArena, (cnt), 2 * sizeof(void*)))

// TLSF Allocator
void*   AllocateTLSFGlobal(size_t size);
void*   ReAllocateTLSFGlobal(void* ptr, size_t size);
void    DeAllocateTLSFGlobal(void* ptr);
void*   AllocZeroTLSFGlobal(size_t count, size_t size);

// FixedPow2Allocator
void  FixedPow2Allocator_Init(FixedPow2Allocator* alloc, size_t initialSize);
void  FixedPow2Allocator_CheckFixGrow(FixedPow2Allocator* alloc, size_t countBytes);
void* FixedPow2Allocator_Allocate(FixedPow2Allocator* alloc, size_t countBytes);
void* FixedPow2Allocator_AllocateUninitialized(FixedPow2Allocator* alloc, size_t countBytes);
void  FixedPow2Allocator_Copy(FixedPow2Allocator* alloc, const FixedPow2Allocator* other);
void* FixedPow2Allocator_TakeOwnership(FixedPow2Allocator* alloc);
void  FixedPow2Allocator_Destroy(FixedPow2Allocator* alloc);

#if defined(__cplusplus)
}
#endif

#endif