
#include "Include/Memory.h"
#include "Include/Common.h"
#include "Include/Algorithm.h"
#include "Extern/tlsf.h"

tlsf_t tlsf;
Arena global_arena;
char app_memory[1 * 1000 * 1000 * 1000];

void* rpcalloc(size_t count, size_t size) 
{
    void* mem = tlsf_malloc(tlsf, count * size);
    MemSet(mem, 0, count * size);
    return mem; 
}

void FixedPow2Allocator_Init(FixedPow2Allocator* alloc, size_t initialSize)
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

void FixedPow2Allocator_CheckFixGrow(FixedPow2Allocator* alloc, size_t countBytes)
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

void* FixedPow2Allocator_Allocate(FixedPow2Allocator* alloc, size_t countBytes)
{
    FixedPow2Allocator_CheckFixGrow(alloc, countBytes);
    void* ptr = alloc->current->ptr + alloc->current->size;
    alloc->current->size += countBytes;
    MemsetZero(ptr, countBytes);
    return ptr;
}

void* FixedPow2Allocator_AllocateUninitialized(FixedPow2Allocator* alloc, size_t countBytes)
{
    FixedPow2Allocator_CheckFixGrow(alloc, countBytes);
    void* ptr = alloc->current->ptr + alloc->current->size;
    alloc->current->size += countBytes;
    return ptr;
}

void FixedPow2Allocator_Copy(FixedPow2Allocator* alloc, const FixedPow2Allocator* other)
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

void* FixedPow2Allocator_TakeOwnership(FixedPow2Allocator* alloc)
{
    void* result = alloc->base;
    alloc->base = NULL;
    return result;
}

void FixedPow2Allocator_Destroy(FixedPow2Allocator* alloc)
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
