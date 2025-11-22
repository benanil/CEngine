#ifndef MEMORY_INCLUDED
#define MEMORY_INCLUDED

#include <stddef.h>

#include "Common.h"
#include "OS.h"
#include "Extern/tlsf.h"

#define ArenaStruct(arena, type)     (ArenaAllocAlign(arena, sizeof(type), ALIGNOF(type)))
#define ArenaArray(arena, type, cnt) (ArenaAllocAlign(arena, sizeof(type) * cnt, ALIGNOF(type)))
#define ArenaAlloc(cnt) (arena_alloc(&global_arena, cnt))

#define rpmalloc(size) tlsf_malloc(tlsf, size)
#define rprealloc(ptr, size) tlsf_realloc(tlsf, ptr, size)
#define rpfree(buff) tlsf_free(tlsf, buff)

typedef struct Arena_ {
	char *buf;
	size_t         buf_len;
	size_t         curr_offset;
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


extern tlsf_t tlsf;
extern Arena global_arena;

void* rpcalloc(size_t count, size_t size);

void FixedPow2Allocator_Init(FixedPow2Allocator* alloc, size_t initialSize);

void FixedPow2Allocator_CheckFixGrow(FixedPow2Allocator* alloc, size_t countBytes);

void* FixedPow2Allocator_Allocate(FixedPow2Allocator* alloc, size_t countBytes);

void* FixedPow2Allocator_AllocateUninitialized(FixedPow2Allocator* alloc, size_t countBytes);

void FixedPow2Allocator_Copy(FixedPow2Allocator* alloc, const FixedPow2Allocator* other);

void* FixedPow2Allocator_TakeOwnership(FixedPow2Allocator* alloc);

void FixedPow2Allocator_Destroy(FixedPow2Allocator* alloc);


static inline void* ArenaGetCurrent(uint64_t size)
{
    if (global_arena.curr_offset + size > global_arena.buf_len)
    {
        ASSERT(0);
        return NULL;
    }
    return global_arena.buf + global_arena.curr_offset;
}

// Shift the given address upwards if/as necessary to// ensure it is aligned to the given number of bytes.
static inline uint64_t AlignAddress(uint64_t addr, uint64_t align)
{
    const uint64_t mask = align - 1;
    ASSERT((align & mask) == 0); // pwr of 2
    return (addr + mask) & ~mask;
}

static inline void* AlignPointer(void* ptr, uint64_t align)
{
    return (void*)AlignAddress((uint64_t)ptr, align);
}

// IMPORTANT: 'align' must be a power of 2 (typically 4, 8 or 16).
static inline void* AllocAligned(uint64_t bytes, uint64_t align)
{
    uint64_t  actualBytes = bytes + align;
    uint8_t* pRawMem = (uint8_t*)rpmalloc(actualBytes);
    uint8_t* pAlignedMem = AlignPointer(pRawMem, align);
    
    if (pAlignedMem == pRawMem)
        pAlignedMem += align;

    uint8_t shift = (uint8_t)(pAlignedMem - pRawMem);
    pAlignedMem[-1] = (uint8_t)(shift & 0xFF);
    return pAlignedMem;
}

static inline void FreeAligned(void* pMem)
{
    uint8_t* pAlignedMem = (uint8_t*)pMem;
    uint64_t shift = pAlignedMem[-1];

    if (shift == 0)
        shift = 256;
    uint8_t* pRawMem = pAlignedMem - shift;
    rpfree(pRawMem);
}

static inline void ArenaInit(Arena *a, size_t backing_buffer_length) {
	size_t aligned_size = OSRoundToPage(backing_buffer_length);
    a->buf = (char*)OSAlloc(aligned_size);
	a->buf_len = aligned_size;
	a->curr_offset = 0;
}

static inline void *ArenaAllocAlign(Arena *a, size_t size, size_t align) {
	size_t curr_ptr = (size_t)a->buf + a->curr_offset;
	size_t offset = AlignAddress(curr_ptr, align);
	offset -= (size_t)a->buf; // Change to relative offset

	ASSERT(offset + size <= a->buf_len);
	void *ptr = &a->buf[offset];
	a->curr_offset = offset + size;
	return ptr;
}

static inline void *arena_alloc(Arena *a, size_t size) {
	return ArenaAllocAlign(a, size, 2 * sizeof(void*)); // default alignment
}


#endif