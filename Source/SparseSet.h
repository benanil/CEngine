#ifndef SS_INCLUDE_SPARSE_SET
#define SS_INCLUDE_SPARSE_SET

#include <stdbool.h>
#include <stdint.h>

#ifndef SSAlloc
    #include <stdlib.h>
    #define SSAlloc(size) malloc(size)
    #define SSRealloc(mem, size) realloc(mem, size)
    #define SSFree(mem) free(mem)
#endif

#ifndef SSMemset
    #include <string.h>
    #define SSMemset(mem, val, size) memset(mem, val, size)
    #define SSMemcpy(dest, src, size) memcpy(dest, src, size)
#endif

#define SSInvalid ~0u

typedef struct SparseSet_ {
    uint32_t* sparse;   // Map: Index -> Dense Position
    uint32_t* dense;    // Map: Dense Position -> Index
    void*     values;   // Packed values
    uint32_t  size;     // Current count
    uint32_t  capacity; // Max index/universe size
    uint32_t  valSize;  // Size of one value in bytes
} SparseSet;

SparseSet SSCreate(uint32_t capacity, uint32_t valueSize);
void      SSDestroy(SparseSet* ss);

bool      SSContains(const SparseSet* ss, uint32_t index);
void*     SSGet(const SparseSet* ss, uint32_t index);
bool      SSTryGet(const SparseSet* ss, uint32_t index, void* out);

void      SSInsert(SparseSet* ss, uint32_t index, const void* val);
void      SSInsertOrAssign(SparseSet* ss, uint32_t index, const void* val);

bool      SSRemove(SparseSet* ss, uint32_t index);

#define SS_DEFINE_TYPE(NAME, TYPE)                                   \
TYPE* SSGet##NAME(const SparseSet* ss, uint32_t index)               \
{                                                                    \
    return (TYPE*)SSGet(ss, index);                                  \
}                                                                    

#ifdef SS_SPARSE_SET_IMPLEMENTATION

SparseSet SSCreate(uint32_t capacity, uint32_t valueSize) {
    SparseSet res;
    res.sparse   = (uint32_t*)SSAlloc(capacity * sizeof(uint32_t));
    res.dense    = (uint32_t*)SSAlloc(capacity * sizeof(uint32_t));
    res.values   = SSAlloc(capacity * valueSize);
    res.capacity = capacity;
    res.valSize  = valueSize;
    res.size     = 0;
    SSMemset(res.sparse, 0xff, capacity * sizeof(uint32_t));
    return res;
}

void SSClear(SparseSet* ss)
{
    SSMemset(ss->sparse, 0xff, ss->capacity * sizeof(uint32_t));
    ss->size = 0;
}

void SSDestroy(SparseSet* ss) {
    SSFree(ss->sparse);
    SSFree(ss->dense);
    SSFree(ss->values);
    SSMemset(ss, 0, sizeof(SparseSet));
}

bool SSContains(const SparseSet* ss, uint32_t index) {
    if (index >= ss->capacity) return false;
    uint32_t denseIdx = ss->sparse[index];
    return (denseIdx < ss->size) && (ss->dense[denseIdx] == index);
}

void* SSGet(const SparseSet* ss, uint32_t index) {
    if (!SSContains(ss, index)) return NULL;
    return (char*)ss->values + (ss->valSize * ss->sparse[index]);
}

bool SSTryGet(const SparseSet* ss, uint32_t index, void* out) {
    void* ptr = SSGet(ss, index);
    if (!ptr) return false;
    SSMemcpy(out, ptr, ss->valSize);
    return true;
}

void SSInsert(SparseSet* ss, uint32_t index, const void* val) {
    uint32_t denseIdx   = ss->size++;
    ss->sparse[index]   = denseIdx;
    ss->dense[denseIdx] = index;
    SSMemcpy((char*)ss->values + (denseIdx * ss->valSize), val, ss->valSize);
}

void SSInsertOrAssign(SparseSet* ss, uint32_t index, const void* val) {
    if (SSContains(ss, index)) {
        uint32_t denseIdx = ss->sparse[index];
        SSMemcpy((char*)ss->values + (denseIdx * ss->valSize), val, ss->valSize);
    } else {
        SSInsert(ss, index, val);
    }
}

bool SSRemove(SparseSet* ss, uint32_t index) {
    if (!SSContains(ss, index)) return false;

    uint32_t denseIdx     = ss->sparse[index];
    uint32_t lastDenseIdx = ss->size - 1;
    uint32_t lastIndex    = ss->dense[lastDenseIdx];

    if (denseIdx != lastDenseIdx) {
        void* targetVal = (char*)ss->values + (denseIdx     * ss->valSize);
        void* sourceVal = (char*)ss->values + (lastDenseIdx * ss->valSize);
        SSMemcpy(targetVal, sourceVal, ss->valSize);

        ss->dense[denseIdx]   = lastIndex;
        ss->sparse[lastIndex] = denseIdx;
    }

    ss->sparse[index] = SSInvalid;
    ss->size--;
    return true;
}

#endif
#endif