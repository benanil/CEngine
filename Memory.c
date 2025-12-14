
#include "Include/Common.h"
#include "Include/Algorithm.h"
#include "Include/TLSF.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Include/OS.h"

tlsf_t GlobalTLSF = NULL;
Arena GlobalArena = { 0, 0, 0 };
char AppMemory[TLSF_MEMORY_SIZE];
char ArenaMemory[ARENA_MEMORY_SIZE];


/* //////////////////////////////////////////////////////////////////////////// */
/*                                    MemAdress                                 */
/* //////////////////////////////////////////////////////////////////////////// */

// Shift the given address upwards if/as necessary to// ensure it is aligned to the given number of bytes.
uint64_t AlignAddress(uint64_t addr, uint64_t align)
{
    const uint64_t mask = align - 1;
    ASSERT((align & mask) == 0); // pwr of 2
    return (addr + mask) & ~mask;
}

// IMPORTANT: 'align' must be a power of 2 (typically 4, 8 or 16).
void* AllocAligned(uint64_t bytes, uint64_t align)
{
    uint64_t  actualBytes = bytes + align;
    uint8_t* pRawMem = (uint8_t*)AllocateTLSFGlobal(actualBytes);
    uint8_t* pAlignedMem = AlignPointer(pRawMem, align);
    
    if (pAlignedMem == pRawMem)
        pAlignedMem += align;

    uint8_t shift = (uint8_t)(pAlignedMem - pRawMem);
    pAlignedMem[-1] = (uint8_t)(shift & 0xFF);
    return pAlignedMem;
}

void FreeAligned(void* pMem)
{
    uint8_t* pAlignedMem = (uint8_t*)pMem;
    uint64_t shift = pAlignedMem[-1];

    if (shift == 0)
        shift = 256;
    uint8_t* pRawMem = pAlignedMem - shift;
    DeAllocateTLSFGlobal(pRawMem);
}

/* //////////////////////////////////////////////////////////////////////////// */
/*                                    TLSF                                     */
/* //////////////////////////////////////////////////////////////////////////// */

static inline void CheckTLSFSize()
{
    if (GlobalTLSF == NULL)
    {
        GlobalTLSF = TLSFCreateWithPool(AppMemory, TLSF_MEMORY_SIZE);
    }
}

void* AllocZeroTLSFGlobal(size_t count, size_t size) 
{
    CheckTLSFSize();
    void* mem = TLSFMalloc(GlobalTLSF, count * size);
    if (mem) MemSet(mem, 0, count * size);
    return mem; 
}

void* AllocateTLSFGlobal(size_t size) 
{
    CheckTLSFSize();
    return TLSFMalloc(GlobalTLSF, size); 
}

void* ReAllocateTLSFGlobal(void* ptr, size_t size)
{
    CheckTLSFSize();
    return TLSFRealloc(GlobalTLSF, ptr, size); 
}

void DeAllocateTLSFGlobal(void* buff)
{
    TLSFFree(GlobalTLSF, buff); 
}

/* //////////////////////////////////////////////////////////////////////////// */
/*                                    Arena                                     */
/* //////////////////////////////////////////////////////////////////////////// */

static inline void CheckArenaSize()
{
    if (GlobalArena.buf == NULL)
    {
        GlobalArena.buf = ArenaMemory;
        GlobalArena.buffLen = ARENA_MEMORY_SIZE;
        GlobalArena.currOffset = 0;
    }
}

uint64_t ArenaRemainingCurrent()
{
    return GlobalArena.buffLen - GlobalArena.currOffset;
}

uint64_t ArenaGetCurrentOfset()
{
    return GlobalArena.currOffset;
}

void ArenaSetCurrentOfset(size_t offset)
{
    GlobalArena.currOffset = offset;
}

void* ArenaPushGlobal(uint64_t size)
{
    CheckArenaSize();
    if (GlobalArena.currOffset + size > GlobalArena.buffLen)
    {
        AX_ERROR("Arena Get Current Failed!");
        ASSERT(0);
        return NULL;
    }
    void* result = GlobalArena.buf + GlobalArena.currOffset;
    GlobalArena.currOffset += size;
    return result;
}

void ArenaPopGlobal(uint64_t size)
{
    if (GlobalArena.currOffset < size)
    {
        AX_WARN("arena trying to free more than necessarry!");
        size = GlobalArena.currOffset;
    }
    GlobalArena.currOffset -= size;
}

void ArenaInit(Arena *a, size_t backing_buffer_length) 
{
	size_t aligned_size = OSRoundToPage(backing_buffer_length);
    a->buf = (char*)OSAlloc(aligned_size);
	a->buffLen = aligned_size;
	a->currOffset = 0;
}

void* ArenaAllocAlign(Arena *a, size_t size, size_t align)
{
	size_t curr_ptr = (size_t)a->buf + a->currOffset;
	size_t offset = AlignAddress(curr_ptr, align);
	offset -= (size_t)a->buf; // Change to relative offset

	ASSERT(offset + size <= a->buffLen);
	void *ptr = &a->buf[offset];
	a->currOffset = offset + size;
	return ptr;
}


/*//////////////////////////////////////////////////////////////////////////*/
/*                          FixedPow2Allocator                              */
/*//////////////////////////////////////////////////////////////////////////*/

void FixedPow2Allocator_Init(FixedPow2Allocator* alloc, size_t initialSize)
{
    // WARNING initial size must be power of two
    ASSERT((initialSize & (initialSize - 1)) == 0);
    alloc->currentCapacity = initialSize;
    alloc->base = (FixedFragment*)AllocateTLSFGlobal(sizeof(FixedFragment));
    alloc->current = alloc->base;
    alloc->base->next = NULL;
    alloc->base->ptr = (char*)AllocateTLSFGlobal(initialSize);
    alloc->base->size = 0;
}

void FixedPow2Allocator_CheckFixGrow(FixedPow2Allocator* alloc, size_t countBytes)
{
    int64_t newSize = alloc->current->size + countBytes;
    if (newSize >= alloc->currentCapacity)
    {
        while (alloc->currentCapacity < newSize)
            alloc->currentCapacity <<= 1;

        alloc->current->next = (FixedFragment*)AllocateTLSFGlobal(sizeof(FixedFragment));
        alloc->current = alloc->current->next;
        alloc->current->next = NULL;
        alloc->current->ptr = (char*)AllocateTLSFGlobal(alloc->currentCapacity);
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
    alloc->base = (FixedFragment*)AllocZeroTLSFGlobal(1, sizeof(FixedFragment));
    alloc->base->next = NULL;
    alloc->base->ptr = (char*)AllocateTLSFGlobal(alloc->currentCapacity);
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
        DeAllocateTLSFGlobal(alloc->base->ptr);
        FixedFragment* oldBase = alloc->base;
        alloc->base = alloc->base->next;
        DeAllocateTLSFGlobal(oldBase);
    }
}
