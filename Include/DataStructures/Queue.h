#ifndef AX_QUEUE
#define AX_QUEUE

#include "../Common.h"
#include "../Memory.h"
#ifdef TEST_QUEUE
    // gcc -std=c99 -Wall -Wextra -I. TestQueue.c -o TestQueue
    #include <stdlib.h>

    void* AllocTLSF(size_t size) { return malloc(size); }
    void* ReAllocTLSF(void* ptr, size_t size) { return realloc(ptr, size); }
    void DeAllocTLSF(void* ptr) { free(ptr); }
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct Queue_
{
    void** ptr  ;
    u32 capacity;  
    u32 front   ;
    u32 rear    ;
    u32 size    ;
} Queue;

typedef enum PQCompare_
{
    PQCompare_Less    = 0,
    PQCompare_Greater = 1
} PQCompare;

// Min Heap data structure
typedef struct PriorityQueue_
{
    void**     heap     ;
    int        size     ;
    int        capacity ;
    PQCompare  compare  ;
} PriorityQueue;

static inline void QueueConstruct(Queue* queue)
{
    queue->capacity = 256;
    queue->front    = 0;
    queue->rear     = 0;
    queue->size     = 0;
    queue->ptr      = (void**)AllocTLSF(queue->capacity * sizeof(void*));
}

static inline void QueueInit(Queue *queue, int _capacity)
{
    queue->capacity = NextPowerOf2_32(_capacity);
    queue->front = 0;
    queue->rear = 0;
    queue->size = 0;
    queue->ptr = (void**)AllocTLSF(queue->capacity * sizeof(void*));
}

static inline void QueueClear(Queue *queue)
{
    DeAllocTLSF(queue->ptr);
    queue->ptr = NULL; 
    queue->capacity = queue->front = queue->rear = queue->size = 0;
}

static inline void QueueReset(Queue *queue)
{
    queue->front = queue->rear = queue->size = 0u;
}

static inline u32  QueSize(Queue *queue) { return queue->size; }
static inline bool QueAny(Queue *queue)   { return QueSize(queue) > 0;  }
static inline bool QueEmpty(Queue *queue) { return QueSize(queue) == 0; }

static inline u32 QueIncrementIndex(Queue *queue, u32 x) 
{
    return (x + 1) & (queue->capacity-1);
}
    
static inline void QueGrowIfNecessary(Queue* queue, u32 _size)
{
    u32 newCapacity = NextPowerOf2_32((int)(queue->size + _size));

    if (AX_LIKELY(newCapacity <= queue->capacity))
        return;

    void** newPtr = (void**)AllocTLSF(newCapacity * sizeof(void*));

    const u32 mask = queue->capacity - 1;

    for (u32 i = 0; i < queue->size; ++i)
        newPtr[i] = queue->ptr[(queue->rear + i) & mask];

    DeAllocTLSF(queue->ptr);

    queue->ptr      = newPtr;
    queue->capacity = newCapacity;
    queue->rear     = 0;
    queue->front    = queue->size;
}

static inline void QueEnqueue(Queue *queue, const void* value)
{
    QueGrowIfNecessary(queue, 1);
    queue->ptr[queue->front & (queue->capacity - 1)] = (void*)value;
    queue->front = QueIncrementIndex(queue, queue->front);
    queue->size++;
}

static inline void QueEnqueueRange(Queue *queue, const void** begin, const void* end)
{
    u32 count = (u32)(((const void**)end) - begin);
    QueGrowIfNecessary(queue, count);
    u32 f = queue->front & (queue->capacity - 1), e = queue->capacity-1;
    for (u32 i = 0; i < count; i++)
    {
        queue->ptr[f++] = (void*)begin[i];
        f &= e;
    }
    queue->front += count;
    queue->size  += count;
}


// returns true if size is enough
static inline bool QueTryDequeueArr(Queue *queue, void** result, u32 count)
{
    if (QueSize(queue) < count)
    {
        return false;
    }
    u32 r = queue->rear, e = queue->capacity-1;
    
    for (u32 i = 0; i < count; i++)
    {
        result[i] = queue->ptr[r++];
        r &= e;
    }
    queue->rear = r;
    queue->size -= count;
    return true;
}

static inline bool QueTryDequeue(Queue *queue, void** out)
{
    if (AX_UNLIKELY(queue->size == 0))
        return false;

    *out = queue->ptr[queue->rear];
    queue->rear = QueIncrementIndex(queue, queue->rear);
    queue->size--;
    return true;
}

static inline void QueDequeueArr(Queue *queue, void** result, u32 count)
{
    ASSERT(QueSize(queue) >= count);
    u32 r = queue->rear, e = queue->capacity-1;
    for (u32 i = 0; i < count; i++)
    {
        result[i] = queue->ptr[r++];
        r &= e; // better than modulo 
    }
    queue->rear = r;
    queue->size -= count;
}

static inline void* QueDequeueSingle(Queue *queue)
{
    ASSERT(queue->size != 0);

    void* val = queue->ptr[queue->rear];
    queue->rear = QueIncrementIndex(queue, queue->rear);
    queue->size--;
    return val;
}

// Routes `QueDequeue` to Array vs Single extraction based on number of arguments passed in `TestQueue.c`
#define GET_QUE_DEQUEUE(_1, _2, _3, NAME, ...) NAME
#define QueDequeue(...) GET_QUE_DEQUEUE(__VA_ARGS__, QueDequeueArr, DUMMY, QueDequeueSingle)(__VA_ARGS__)


////////////                PriorityQueue                /////////////


static inline void PriorityQueueInit(PriorityQueue* pq, int _size)
{
    pq->size = 0;
    pq->capacity = CalculateArrayGrowth(_size);
    pq->heap = (void**)AllocTLSF(pq->capacity * sizeof(void*));
    pq->compare = PQCompare_Less;
}

// Function macro ensures `PriorityQueue pq;` is safely treated as a type while `PriorityQueue(&pq, 10)` acts as a function.
#define PriorityQueue(...) PriorityQueueInit(__VA_ARGS__)

static inline bool PQEmpty(PriorityQueue* pq)  { return pq->size == 0; }

static inline void PQGrowIfNecessarry(PriorityQueue* pq, int adition)
{
    if (pq->size + adition > pq->capacity)
    {
        int newCapacity = pq->size + adition <= 256 ? 256 : CalculateArrayGrowth(pq->size + adition);
        if (pq->heap)
            pq->heap = (void**)ReAllocTLSF(pq->heap, newCapacity * sizeof(void*));
        else
            pq->heap = (void**)AllocTLSF(newCapacity * sizeof(void*));
        pq->capacity = newCapacity;
    }
}

static inline bool PQCompareFn(PriorityQueue* pq, const void* a, const void* b)
{
    if (pq->compare == PQCompare_Less)    return a < b;
    if (pq->compare == PQCompare_Greater) return a > b;
    else { ASSERT(0); return 0; }
}

static inline void PQHeapifyUp(PriorityQueue* pq, int index, bool force)
{
    (void)force;

    while (index != 0)
    {
        int parent = (index - 1) >> 1;

        if (PQCompareFn(pq, pq->heap[index], pq->heap[parent]))
        {
            XSWAP(void*, pq->heap[parent], pq->heap[index]);
            index = parent;
        }
        else
            break;
    }
}

static inline void PQHeapifyDown(PriorityQueue* pq, int index)
{
    while (true)
    {
        int left  = (index << 1) + 1;
        int right = left + 1;
        int best  = index;

        if (left < pq->size &&
            PQCompareFn(pq, pq->heap[left], pq->heap[best]))
        {
            best = left;
        }

        if (right < pq->size &&
            PQCompareFn(pq, pq->heap[right], pq->heap[best]))
        {
            best = right;
        }

        if (best == index)
            break;

        XSWAP(void*, pq->heap[index], pq->heap[best]);
        index = best;
    }
}

static inline void PQClear(PriorityQueue* pq)
{
    if (pq->heap)
    {
        DeAllocTLSF(pq->heap);
        pq->heap = NULL;
        pq->size  = pq->capacity = 0;
    }
}

static inline void PQPush(PriorityQueue* pq, const void* value) 
{
    PQGrowIfNecessarry(pq, 1);
    pq->heap[pq->size++] = (void*)value; 
    PQHeapifyUp(pq, pq->size - 1, false); 
}

static inline void PriorityQueueRange(PriorityQueue* pq, const void** begin, const void** end)
{
    pq->heap = NULL; pq->size = 0; pq->capacity = 0;
    pq->compare = PQCompare_Less;
    int count = (int)(end - begin);
    PQGrowIfNecessarry(pq, count);
    for(int i = 0; i < count; i++) {
        PQPush(pq, begin[i]);
    }
}

static inline void* PQRemoveAt(PriorityQueue* pq, int index)
{
    ASSERT(!(index > pq->size - 1 || index < 0));
    void* res = pq->heap[index];
    pq->heap[index] = pq->heap[pq->size - 1]; // Correct basic array behavior before re-heapifying
    pq->size--;
    if (index < pq->size) {
        PQHeapifyUp(pq, index, true);
        PQHeapifyDown(pq, index);
    }
    return res;
}

static inline void PQPop(PriorityQueue* pq)
{
    ASSERT(!PQEmpty(pq));
    --pq->size;
    if (pq->size)
    {
        pq->heap[0] = pq->heap[pq->size];
        PQHeapifyDown(pq, 0);
    }
}

static inline void* PQTop(PriorityQueue* pq)
{
    ASSERT(!PQEmpty(pq));
    return pq->heap[0];
}

#if defined(__cplusplus)
}
#endif


#endif // AX_QUEUE