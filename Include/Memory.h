#ifndef MEMORY_INCLUDED
#define MEMORY_INCLUDED

#include <stddef.h>
#include <stdint.h>

#ifndef TLSF_MEMORY_SIZE
    #define TLSF_MEMORY_SIZE (1024ull * 1000ull * 1000ull)
#endif

#ifndef ARENA_MEMORY_SIZE
    #define ARENA_MEMORY_SIZE (256 * 1024 * 1024) /* 256 mb */ 
#endif

#ifndef DEFAULT_ALIGN
    #define DEFAULT_ALIGN (2 * sizeof(void*))
#endif

#define DEFER(_end_) for(int (_defer_##__LINE__) = 0; (_defer_##__LINE__) < 1; (_defer_##__LINE__)++, _end_)

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct Arena_ {
    char*  buf;
    size_t buffLen;
    size_t currOffset;
} Arena;

typedef struct ArenaMark_ {
    size_t offset;
} ArenaMark;

typedef struct FixedFragment_ {
    struct FixedFragment_* next;
    char*  ptr;
    size_t size;
} FixedFragment;

typedef struct FixedPow2Allocator_ {
    size_t         currentCapacity;
    FixedFragment* base;
    FixedFragment* current;
} FixedPow2Allocator;

extern Arena GlobalArena;

uint64_t  AlignAddress(uint64_t addr, uint64_t align);
void*     AlignPointer(void* ptr, uint64_t align);
void*     AllocAligned(uint64_t bytes, uint64_t align);
void      FreeAligned(void* pMem);

void      ArenaInit(Arena* a, size_t backing_buffer_length);
void      ArenaFree(Arena* a);
void      ArenaReset(Arena* a);
void*     ArenaAllocAlign(Arena* a, size_t size, size_t align);
void      ArenaPopAligned(Arena* a, void* ptr, size_t size, size_t align);
void*     ArenaAlloc(Arena* a, size_t size);
void*     ArenaAllocZero(Arena* a, size_t size);
size_t    ArenaRemaining(Arena* a);
ArenaMark ArenaSave(Arena* a);
void      ArenaRestore(Arena* a, ArenaMark mark);

void      InitGlobalArena();
Arena*    GetGlobalArena();
void*     ArenaPushGlobal(uint64_t size);
void      ArenaPopGlobal(uint64_t size);
uint64_t  ArenaRemainingCurrent(void);
uint64_t  ArenaGetCurrentOffset(void);
void      ArenaSetCurrentOffset(size_t offset);

size_t OSGetPageSize(void);
size_t OSRoundToPage(size_t size);
void*  OSAlloc(size_t size);
int    OSFree(void *ptr, size_t size);

#define ArenaStruct(arena, type)     ((type*)ArenaAllocAlign(arena, sizeof(type), _Alignof(type)))
#define ArenaArray(arena, type, cnt) ((type*)ArenaAllocAlign(arena, sizeof(type) * (cnt), _Alignof(type)))
#define ArenaAllocGlobal(cnt)        (ArenaAllocAlign(&GlobalArena, (cnt), DEFAULT_ALIGN))

void* AllocateTLSFGlobal(size_t size);
void* ReAllocateTLSFGlobal(void* ptr, size_t size);
void  DeAllocateTLSFGlobal(void* ptr);
void* AllocZeroTLSFGlobal(size_t count, size_t size);

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

