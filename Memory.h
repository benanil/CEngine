#ifndef MEMORY_INCLUDED
#define MEMORY_INCLUDED

#include "Common.h"
#include "Extern/tlsf.c"
#include "Algorithm.h"

#define rpmalloc(size) tlsf_malloc(tlsf, size)
#define rprealloc(ptr, size) tlsf_realloc(tlsf, ptr, size)
#define rpfree(buff) tlsf_free(tlsf, buff)

extern void* tlsf;

static inline void* rpcalloc(size_t count, size_t size) 
{
    void* mem = tlsf_malloc(tlsf, count * size);
    MemSet(mem, 0, count * size, 0);
    return mem; 
}

// Shift the given address upwards if/as necessary to// ensure it is aligned to the given number of bytes.
static inline uint64_t AlignAddress(uint64_t addr, uint64_t align)
{
    uint64_t mask = align - 1;
    ASSERT((align & mask) == 0); // pwr of 2
    return (addr + mask) & ~mask;
}

// Shift the given pointer upwards if/as necessary to// ensure it is aligned to the given number of bytes.
static inline void* AlignPointer(void* ptr, uint64_t align)
{
    return (void*)AlignAddress((uint64_t)ptr, align);
}

// Aligned allocation function. IMPORTANT: 'align'// must be a power of 2 (typically 4, 8 or 16).
static inline void* AllocAligned(uint64_t bytes, uint64_t align)
{
    // Allocate 'align' more bytes than we need.
    uint64_t  actualBytes = bytes + align;
    // Allocate unaligned block.
    uint8_t* pRawMem = (uint8_t*)rpmalloc(actualBytes);
    // Align the block. If no alignment occurred,// shift it up the full 'align' bytes so we// always have room to store the shift.
    uint8_t* pAlignedMem = AlignPointer(pRawMem, align);
    
    if (pAlignedMem == pRawMem)
        pAlignedMem += align;
    // Determine the shift, and store it.// (This works for up to 256-byte alignment.)
    uint8_t shift = (uint8_t)(pAlignedMem - pRawMem);
    ASSERT(shift > 0 && shift <= 256);
    pAlignedMem[-1] = (uint8_t)(shift & 0xFF);
    return pAlignedMem;
}

static inline void FreeAligned(void* pMem)
{
    ASSERT(pMem);
    // Convert to U8 pointer.
    uint8_t* pAlignedMem = (uint8_t*)pMem;
    // Extract the shift.
    uint64_t  shift = pAlignedMem[-1];
    if (shift == 0)
        shift = 256;
    // Back up to the actual allocated address,
    uint8_t* pRawMem = pAlignedMem - shift;
    rpfree(pRawMem);
}

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

static inline void FixedPow2Allocator_Init(FixedPow2Allocator* alloc, size_t initialSize)
{
    // WARNING initial size must be power of two
    ASSERT((initialSize & (initialSize - 1)) == 0);
    alloc->currentCapacity = initialSize;
    alloc->base = (FixedFragment*)rpmalloc(sizeof(FixedFragment));
    alloc->current = alloc->base;
    alloc->base->next = NULL;
    alloc->base->ptr = (char*)rpmalloc(initialSize);
    alloc->base->size = 0;
}

static inline void FixedPow2Allocator_CheckFixGrow(FixedPow2Allocator* alloc, size_t countBytes)
{
    int64_t newSize = alloc->current->size + countBytes;
    if (newSize >= alloc->currentCapacity)
    {
        while (alloc->currentCapacity < newSize)
            alloc->currentCapacity <<= 1;

        alloc->current->next = (FixedFragment*)rpmalloc(sizeof(FixedFragment));
        alloc->current = alloc->current->next;
        alloc->current->next = NULL;
        alloc->current->ptr = (char*)rpmalloc(alloc->currentCapacity);
        alloc->current->size = 0;
    }
}

static inline void* FixedPow2Allocator_Allocate(FixedPow2Allocator* alloc, size_t countBytes)
{
    FixedPow2Allocator_CheckFixGrow(alloc, countBytes);
    void* ptr = alloc->current->ptr + alloc->current->size;
    alloc->current->size += countBytes;
    MemsetZero(ptr, countBytes);
    return ptr;
}

static inline void* FixedPow2Allocator_AllocateUninitialized(FixedPow2Allocator* alloc, size_t countBytes)
{
    FixedPow2Allocator_CheckFixGrow(alloc, countBytes);
    void* ptr = alloc->current->ptr + alloc->current->size;
    alloc->current->size += countBytes;
    return ptr;
}

static inline void FixedPow2Allocator_Copy(FixedPow2Allocator* alloc, FixedPow2Allocator* other)
{
    if (!other->base) return;

    size_t totalSize = 0;
    FixedFragment* start = other->base;
    while (start)
    {
        totalSize += start->size;
        start = start->next;
    }

    alloc->currentCapacity = 1ULL << (64 - LeadingZeroCount64(totalSize));
    alloc->base = (FixedFragment*)rpcalloc(1, sizeof(FixedFragment));
    alloc->base->next = NULL;
    alloc->base->ptr = (char*)rpmalloc(alloc->currentCapacity);
    alloc->base->size = totalSize;
    alloc->current = alloc->base;

    // copy other's memory
    char* curr = alloc->base->ptr;
    start = other->base;
    while (start)
    {
        SmallMemCpy(curr, start->ptr, start->size);
        curr += start->size;
        start = start->next;
    }
}

static inline void* FixedPow2Allocator_TakeOwnership(FixedPow2Allocator* alloc)
{
    void* result = alloc->base;
    alloc->base = NULL;
    return result;
}

static inline void FixedPow2Allocator_Destroy(FixedPow2Allocator* alloc)
{
    if (!alloc->base) return;
    while (alloc->base)
    {
        rpfree(alloc->base->ptr);
        FixedFragment* oldBase = alloc->base;
        alloc->base = alloc->base->next;
        rpfree(oldBase);
    }
}

#endif