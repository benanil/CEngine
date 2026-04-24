#include "Include/Memory.h"
#include "Include/Common.h"
#include "Include/Algorithm.h"
#include "Include/Platform.h"
#include "Include/OS.h"

#include "Extern/tlsf.h"

Arena GlobalArena = { 0, 0, 0 };
tlsf_t GlobalTLSF = NULL;
char  ArenaMemory[ARENA_MEMORY_SIZE];
char  TLSFMemory[TLSF_MEMORY_SIZE];

void InitGlobalArena()
{
    GlobalArena.buf = ArenaMemory;
    GlobalArena.buffLen = ARENA_MEMORY_SIZE;
    GlobalArena.currOffset = 0;
    GlobalTLSF = tlsf_create_with_pool(TLSFMemory, TLSF_MEMORY_SIZE);
}

Arena* GetGlobalArena()
{
    return &GlobalArena;
}

uint64_t AlignAddress(uint64_t addr, uint64_t align)
{
    const uint64_t mask = align - 1;
    ASSERT((align & mask) == 0);
    return (addr + mask) & ~mask;
}

static bool CheckTLSFFail(void* ptr)
{
    if (ptr == NULL)
    {
        AX_ERROR("TLSF Memory Allocating failed not enough memory total tlsf size:%d mb", TLSF_MEMORY_SIZE/1024/1024);
        return false;
    }
    return true;
}

void* AlignPointer(void* ptr, uint64_t align)
{
    return (void*)AlignAddress((uint64_t)ptr, align);
}

void* AllocAligned(uint64_t bytes, uint64_t align)
{
    uint64_t actualBytes = bytes + align;
    uint8_t* pRawMem     = (uint8_t*)tlsf_malloc(GlobalTLSF, actualBytes);
    if (!CheckTLSFFail(pRawMem)) return NULL;
    uint8_t* pAlignedMem = (uint8_t*)AlignPointer(pRawMem, align);

    if (pAlignedMem == pRawMem)
        pAlignedMem += align;

    uint8_t shift      = (uint8_t)(pAlignedMem - pRawMem);
    pAlignedMem[-1]    = (uint8_t)(shift & 0xFF);
    return pAlignedMem;
}

void FreeAligned(void* pMem)
{
    uint8_t* pAlignedMem = (uint8_t*)pMem;
    uint64_t shift       = pAlignedMem[-1];
    if (shift == 0) shift = 256;

    uint8_t* pRawMem = pAlignedMem - shift;
    tlsf_free(GlobalTLSF, pRawMem);
}

void* AllocZeroTLSFGlobal(size_t count, size_t size)
{
    void* ptr = tlsf_malloc(GlobalTLSF, count * size);
    if (!CheckTLSFFail(ptr)) return NULL;
    MemsetZero(ptr, count * size);
    return ptr;
}

void* AllocateTLSFGlobal(size_t size)
{
    void* ptr = tlsf_malloc(GlobalTLSF, size);
    if (!CheckTLSFFail(ptr)) return NULL;
    MemSet(ptr, 0xCD, size);
    return ptr;
}

void* ReAllocateTLSFGlobal(void* ptr, size_t size)
{
    void* res = tlsf_realloc(GlobalTLSF, ptr, size);
    if (!CheckTLSFFail(res)) return NULL;
    return res;
}

void DeAllocateTLSFGlobal(void* buff)
{
    tlsf_free(GlobalTLSF, buff);
}

static inline void CheckArenaSize(void)
{
    if (GlobalArena.buf == NULL)
    {
        GlobalArena.buf        = ArenaMemory;
        GlobalArena.buffLen    = ARENA_MEMORY_SIZE;
        GlobalArena.currOffset = 0;
    }
}

void ArenaInit(Arena* a, size_t backing_buffer_length)
{
    size_t aligned_size = OSRoundToPage(backing_buffer_length);
    a->buf        = (char*)OSAlloc(aligned_size);
    a->buffLen    = aligned_size;
    a->currOffset = 0;
}

void ArenaFree(Arena* a)
{
    if (a->buf)
    {
        OSFree(a->buf, a->buffLen);
        a->buf        = NULL;
        a->buffLen    = 0;
        a->currOffset = 0;
    }
}

void ArenaReset(Arena* a)
{
    a->currOffset = 0;
}

void* ArenaAllocAlign(Arena* a, size_t size, size_t align)
{
    size_t curr_ptr = (size_t)a->buf + a->currOffset;
    size_t offset   = AlignAddress(curr_ptr, align);
    offset         -= (size_t)a->buf;

    ASSERT(offset + size <= a->buffLen);
    void* ptr      = &a->buf[offset];
    a->currOffset  = offset + size;
    return ptr;
}

void ArenaPopAligned(Arena* a, void* ptr, size_t size, size_t align)
{
    size_t curr_ptr = (size_t)a->buf + a->currOffset;
    size_t aligned  = AlignAddress((size_t)a->buf + (a->currOffset - size), align);
    size_t padded   = curr_ptr - aligned;
    ASSERT(a->currOffset >= padded);
    a->currOffset -= padded;
}

void* ArenaAlloc(Arena* a, size_t size)
{
    return ArenaAllocAlign(a, size, DEFAULT_ALIGN);
}

void* ArenaAllocZero(Arena* a, size_t size)
{
    void* ptr = ArenaAllocAlign(a, size, DEFAULT_ALIGN);
    MemSet(ptr, 0, size);
    return ptr;
}

size_t ArenaRemaining(Arena* a)
{
    return a->buffLen - a->currOffset;
}

ArenaMark ArenaSave(Arena* a)
{
    ArenaMark mark;
    mark.offset = a->currOffset;
    return mark;
}

void ArenaRestore(Arena* a, ArenaMark mark)
{
    ASSERT(mark.offset <= a->currOffset);
    a->currOffset = mark.offset;
}

uint64_t ArenaRemainingCurrent(void)
{
    return GlobalArena.buffLen - GlobalArena.currOffset;
}

uint64_t ArenaGetCurrentOffset(void)
{
    return GlobalArena.currOffset;
}

void ArenaSetCurrentOffset(size_t offset)
{
    GlobalArena.currOffset = offset;
}

void* ArenaPushGlobal(uint64_t size)
{
    CheckArenaSize();
    if (GlobalArena.currOffset + size > GlobalArena.buffLen)
    {
        AX_ERROR("Arena push failed: out of memory");
        return NULL;
    }
    void* result           = GlobalArena.buf + GlobalArena.currOffset;
    GlobalArena.currOffset += size;
    return result;
}

void ArenaPopGlobal(uint64_t size)
{
    if (GlobalArena.currOffset < size)
    {
        AX_WARN("arena trying to free more than allocated");
        size = GlobalArena.currOffset;
    }
    GlobalArena.currOffset -= size;
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
    #if defined(_DEBUG) || defined(DEBUG) || defined(Debug)
    MemSet(ptr, 0xCD, countBytes);
    #endif
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
    // test {} 
    if (!alloc->base) return;
    while (alloc->base)
    {
        DeAllocateTLSFGlobal(alloc->base->ptr);
        FixedFragment* oldBase = alloc->base;
        alloc->base = alloc->base->next;
        DeAllocateTLSFGlobal(oldBase);
    }
}
