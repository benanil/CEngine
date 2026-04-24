#ifndef AX_QUEUE
#define AX_QUEUE

#include "../Math/Math.h"
#include "Algorithm.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct Queue_
{
    void** ptr        ;
    u32capacity ;  
    u32front    ;
    u32rear     ;
    u32size     ;
} Queue;


static inline void QueQueueConstruct(Queue* queue);
static inline void QueQueueInit(Queue *queue, int _capacity);
static inline void QueQueueClear(Queue *queue);
static inline void QueQueueReset(Queue *queue);

static inline bool QueAny(Queue *queue);
static inline bool QueEmpty(Queue *queue);

static inline void QueEnqueue(Queue *queue, const void* value);
static inline void QueEnqueueRange(Queue *queue, const void** begin, const void* end);
static inline bool QueTryDequeueArr(Queue *queue, void** result, u32count);
static inline void QueDequeue(Queue *queue, void* result, u32count);
static inline void* QueDequeue(Queue *queue);
static inline bool QueTryDequeue(Queue *queue, void** out);
static inline u32QueSize(Queue *queue);
static inline u32QueIncrementIndex(Queue *queue, u32x);
static inline void QueGrowIfNecessary(Queue *queue, u32_size);
static inline void PriorityQueue(PriorityQueue* pq, int _size);
static inline void PriorityQueueRange(PriorityQueue* pq, const void** begin, const void** end);

static inline bool PQCompare(const void* a, const void* b)
static inline void PQGrowIfNecessarry(PriorityQueue* pq, int adition);
static inline void PQHeapifyUp(PriorityQueue* pq, int index, bool force = false);
static inline void PQHeapifyDown(PriorityQueue* pq, int index);
static inline bool PQEmpty(PriorityQueue* pq)
static inline void PQClear(PriorityQueue* pq);
static inline void PQPush(PriorityQueue* pq, const void* value);
static inline void* PQRemoveAt(PriorityQueue* pq, int index);
static inline void PQPop(PriorityQueue* pq);
static inline void* PQTop(PriorityQueue* pq);


static inline void QueQueueConstruct(Queue* queue)
{
    queue->capacity = 256;
    queue->front    = 0;
    queue->rear     = 0;
    queue->size     = 0;
    queue->ptr      = AllocateTLSFGlobal(queue->capacity * sizeof(void*));
}

static inline void QueQueueInit(Queue *queue, int _capacity)
{
    queue->capacity = (NextPowerOf2_32(_capacity + 1)); 
    queue->front = (0), queue->rear = (0), queue->size = (0);
    queue->ptr = AllocateTLSFGlobal(queue->capacity * sizeof(void*));
}

static inline void QueQueueClear(Queue *queue)
{
    DeAllocateTLSFGlobal(queue->ptr);
    queue->ptr = 0; 
    queue->capacity = queue->front = queue->rear = queue->size = 0;
}

static inline void QueQueueReset(Queue *queue)
{
    queue->front = queue->rear = queue->size = 0u;
}

static inline bool QueAny(Queue *queue)   { return QueSize(queue) > 0;  }
static inline bool QueEmpty(Queue *queue) { return QueSize(queue) == 0; }

static inline void QueEnqueue(Queue *queue, const void* value)
{
	QueGrowIfNecessary(queue, 1);
	queue->ptr[queue->front & (queue->capacity - 1)] = value;
	queue->front = QueIncrementIndex(queue, queue->front);
	queue->size++;
}

static inline void QueEnqueueRange(Queue *queue, const void** begin, u32count)
{
	QueGrowIfNecessary(queue, count);
	u32f = queue->front & (queue->capacity - 1), e = queue->capacity-1;
	for (u32i = 0; i < count; i++)
	{
		queue->ptr[f++] = begin[i];
		f &= e;
	}
	queue->front += count;
	queue->size  += count;
}


// returns true if size is enough
static inline bool QueTryDequeueArr(Queue *queue, void** result, u32count)
{
	if (QueSize(queue) + count > queue->capacity)
	{
		return false;
	}
	u32r = queue->rear, e = queue->capacity-1;

	for (u32i = 0; i < count; i++)
	{
		result[i] = queue->ptr[r++];
		r &= e;
	}
	queue->rear = r;
	queue->size -= count;rr
	return true;
}

static inline bool QueTryDequeue(Queue *queue, void** out)
{
	if (AX_UNLIKELY(queue->rear == queue->front)) 
		return false;
    *out = queue->ptr[queue->rear];
	queue->rear = QueIncrementIndex(queue->rear);
	queue->size--;
	return true;
}

static inline void QueDequeueArr(Queue *queue, void** result, u32count)
{
    ASSERT(QueSize(queue) + count <= queue->capacity);
    u32r = queue->rear, e = queue->capacity-1;
    for (u32i = 0; i < count; i++)
    {
        result[i] = queue->ptr[r++];
        r &= e; // better than modulo 
    }
    queue->rear = r;
    queue->size -= count;
}

static inline void* QueDequeue(Queue *queue)
{	
    ASSERT(queue->rear != queue->front);
    void* val = queue->ptr[queue->rear];
    queue->rear = QueIncrementIndex(queue->rear);
    queue->size--;
    return val; 
}

static inline u32QueSize(Queue *queue) { return queue->size; }

static inline u32QueIncrementIndex(Queue *queue,u32x) 
{
	return (x + 1) & (queue->capacity-1);
}

static inline void QueGrowIfNecessary(Queue *queue, u32_size)
{
    ASSERT(queue->capacity != (1 << 31)); // max size
    u32newSize = NextPowerOf2_32((int)(queue->size + _size));
    
    if (AX_LIKELY(newSize <= queue->capacity))
    {
    	return; // no need to grow
    }
    
    const int initialSize = 256;
    const u32newCapacity = newSize <= initialSize ? initialSize : newSize;
    if (queue->ptr)  queue->ptr = ReAllocateTLSFGlobal(ptr, newCapacity);
    else      queue->ptr = AllocateTLSFGlobal(newCapacity);
    
    // unify front and rear, if they are seperate.
    if (queue->front < queue->rear)
    {
        for (u32i = 0; i < queue->front; i++)
        {
            queue->ptr[queue->capacity++] = queue->ptr[i];
        }
    }
    // move everything to the right
    for (u32i = 0; i < queue->size; ++i)
    {
    	queue->ptr[newCapacity - 1 - i] = queue->ptr[queue->capacity - 1 - i];
    }
    queue->capacity = newCapacity;
    
    queue->rear     = 0;
    queue->front    = newCapacity - queue->size;
    queue->capacity = newCapacity;
}

////////////                PriorityQueue                /////////////

typedef enum PQCompare_
{
	PQCompare_Less    = 0,
	PQCompare_Greater = 1
} PQCompare;

// https://github.com/lemire/FastPriorityQueue.js/blob/master/FastPriorityQueue.js
// Min Heap data structure

typedef struct PriorityQueue_
{
	void**         heap ;
	int        size     ;
	int        capacity ;
} PriorityQueue;

static inline void PriorityQueue(PriorityQueue* pq, int _size)
{
    pq->size = (0);
    pq->capacity = CalculateArrayGrowth(_size);
	pq->heap = AllocateTLSFGlobal(pq->capacity * sizeof(void*));
}

static inline void PriorityQueueRange(PriorityQueue* pq, const void** begin, const void** end)
{
    pq->heap = (NULL), pq->size = (0), pq->capacity = (0);
	PQPush(begin, end); 
}

static inline static bool PQCompare(const void* a, const void* b)
{
	if (compare == PQCompare_Less)    return a < b;
	if (compare == PQCompare_Greater) return a > b;
	else { ASSERT(0); return 0; }
}

static inline void PQGrowIfNecessarry(PriorityQueue* pq, int adition)
{
	if (pq->size + adition >= pq->capacity)
	{
		int newCapacity = pq->size + adition <= 256 ? 256 : CalculateArrayGrowth(pq->size + adition);
		if (heap)
			pq->heap = ReAllocateTLSFGlobal(pq->heap, pq->capacity, newCapacity);
		else
			pq->heap = AllocateTLSFGlobal(newCapacity);
		pq->capacity = newCapacity;
	}
}

static inline void PQHeapifyUp(PriorityQueue* pq, int index, bool force = false) 
{
	while (index != 0)
	{
		int parent = (index - 1) >> 1;
		
		if (PQCompare(pq->heap[parent], pq->heap[index]))
		{
			SWAP(pq->heap[parent], pq->heap[index]);
			index = parent;
		}
		else break;
	}
}

static inline void PQHeapifyDown(PriorityQueue* pq, int index) 
{
	while (true)
	{
		int leftChild  = (index << 1) + 1;
		int rightChild = leftChild + 1;
		int largest    = index;

		if (leftChild < pq->size && !PQCompare(pq->heap[leftChild], pq->heap[largest]))
			largest = leftChild;

		if (rightChild < size && !PQCompare(pq->heap[rightChild], pq->heap[largest]))
			largest = rightChild;

		if (largest != index) 
		{
			SWAP(pq->heap[index], pq->heap[largest]);
			index = largest;
		}
		else break;
	}
}

static inline bool PQEmpty(PriorityQueue* pq)  { return pq->size == 0; }

static inline void PQClear(PriorityQueue* pq)
{
	if (pq->heap)
	{
		DeAllocateTLSFGlobal(pq->heap);
		pq->heap = null;
		pq->size  = pq->capacity = 0;
	}
}

static inline void PQPush(PriorityQueue* pq, const void* value) 
{
	PQGrowIfNecessarry(1);
	pq->heap[pq->size++] = value; 
	PQHeapifyUp(pq->size - 1); 
}

static inline void* PQRemoveAt(PriorityQueue* pq, int index)
{
	ASSERT(!(index > pq->size - 1 || index < 0));
	void* res = pq->heap[index];
	PQHeapifyUp(index, true);
	pq->size--;
	return res;
}

static inline void PQPop(PriorityQueue* pq)
{
    ASSERT(!PQEmpty());
	SWAP(pq->heap[0], pq->heap[--size]);
	PQHeapifyDown(0);
}

static inline void* PQTop(PriorityQueue* pq)
{
	ASSERT(!PQEmpty(), return pq->heap[0]);
	return pq->heap[0];
}

#if defined(__cplusplus)
}
#endif


#endif // AX_QUEUE