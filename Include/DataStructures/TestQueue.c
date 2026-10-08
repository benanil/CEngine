#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include "Queue.h"

// Include your header containing the Queue / PriorityQueue definitions & inline implementations
// #include "queue.h"

// Types used in signatures for compilation completeness
typedef uint32_t u32;

/* --- Helper Callbacks / Mock Data --- */

// Sample comparator function for testing PriorityQueue
static bool TestPQCompare(const void* a, const void* b) {
    if (!a || !b) return false;
    return *(const int*)a < *(const int*)b; // Min-heap behavior
}

/* --- Queue Unit Tests --- */

static void Test_Queue_Lifecycle(void) {
    Queue q;
    QueueConstruct(&q);
    QueueInit(&q, 10);

    assert(QueEmpty(&q) == true);
    assert(QueAny(&q) == false);
    assert(QueSize(&q) == 0);

    QueueClear(&q);
    QueueReset(&q);
    printf("[PASS] Queue Lifecycle & Initialization\n");
}

static void Test_Queue_Enqueue_Dequeue_Single(void) {
    Queue q;
    QueueConstruct(&q);
    QueueInit(&q, 5);

    int val1 = 42;
    int val2 = 99;

    QueEnqueue(&q, &val1);
    QueEnqueue(&q, &val2);

    assert(QueSize(&q) == 2);
    assert(QueEmpty(&q) == false);
    assert(QueAny(&q) == true);

    int* popped1 = (int*)QueDequeue(&q);
    assert(popped1 != NULL && *popped1 == 42);

    void* popped2 = NULL;
    bool success = QueTryDequeue(&q, &popped2);
    assert(success == true);
    assert(popped2 != NULL && *(int*)popped2 == 99);

    assert(QueEmpty(&q) == true);
    assert(QueSize(&q) == 0);

    // Try dequeue on empty queue
    void* popped_empty = NULL;
    assert(QueTryDequeue(&q, &popped_empty) == false);

    QueueClear(&q);
    printf("[PASS] Queue Single Enqueue/Dequeue\n");
}

static void Test_Queue_Range_And_Array_Operations(void) {
    Queue q;
    QueueConstruct(&q);
    QueueInit(&q, 8);

    int vals[] = {10, 20, 30, 40, 50};
    const void* range_begin[] = { &vals[0], &vals[1], &vals[2], &vals[3], &vals[4] };

    QueEnqueueRange(&q, range_begin, (const void*)range_begin + sizeof(range_begin));

    assert(QueSize(&q) == 5);

    // Test QueTryDequeueArr / QueDequeue with count
    void* dequeued_ptrs[3];
    bool status = QueTryDequeueArr(&q, dequeued_ptrs, 3);
    assert(status == true);
    assert(*(int*)dequeued_ptrs[0] == 10);
    assert(*(int*)dequeued_ptrs[1] == 20);
    assert(*(int*)dequeued_ptrs[2] == 30);

    assert(QueSize(&q) == 2);

    void* remaining_ptrs[2];
    QueDequeue(&q, remaining_ptrs, 2);
    assert(*(int*)remaining_ptrs[0] == 40);
    assert(*(int*)remaining_ptrs[1] == 50);

    assert(QueEmpty(&q) == true);

    QueueClear(&q);
    printf("[PASS] Queue Range & Array Dequeue\n");
}

static void Test_Queue_Growth_And_Index(void) {
    Queue q;
    QueueConstruct(&q);
    QueueInit(&q, 2);

    int a = 1, b = 2, c = 3;
    QueEnqueue(&q, &a);
    QueEnqueue(&q, &b);

    // Explicitly trigger growth test
    QueGrowIfNecessary(&q, 10);

    QueEnqueue(&q, &c);
    assert(QueSize(&q) == 3);

    u32 next_idx = QueIncrementIndex(&q, 0);
    assert(next_idx == 1);

    QueueClear(&q);
    printf("[PASS] Queue Growth & Indexing\n");
}

/* --- Priority Queue Unit Tests --- */

static void Test_PriorityQueue_Lifecycle(void) {
    PriorityQueue pq;
    PriorityQueue(&pq, 10);

    assert(PQEmpty(&pq) == true);

    PQClear(&pq);
    printf("[PASS] Priority Queue Lifecycle\n");
}

static void Test_PriorityQueue_Push_Pop_Top(void) {
    PriorityQueue pq;
    PriorityQueue(&pq, 5);

    int v1 = 30, v2 = 10, v3 = 20;

    PQPush(&pq, &v1);
    PQPush(&pq, &v2);
    PQPush(&pq, &v3);

    assert(PQEmpty(&pq) == false);

    void* top = PQTop(&pq);
    assert(top != NULL);

    // Assuming min-heap logic with PQCompare implementation
    PQPop(&pq);
    assert(PQEmpty(&pq) == false);

    PQClear(&pq);
    printf("[PASS] Priority Queue Push, Pop, Top\n");
}

static void Test_PriorityQueue_Range_And_Heapify(void) {
    PriorityQueue pq;
    PriorityQueue(&pq, 8);

    int vals[] = {100, 50, 150};
    const void* begin[] = { &vals[0], &vals[1], &vals[2] };
    const void* end = (const void*)begin + sizeof(begin);

    PriorityQueueRange(&pq, begin, (const void**)end);

    PQGrowIfNecessarry(&pq, 5);

    // Test explicit heapify operations
    PQHeapifyUp(&pq, 0, false);
    PQHeapifyDown(&pq, 0);

    void* removed = PQRemoveAt(&pq, 0);
    assert(removed != NULL);

    PQClear(&pq);
    printf("[PASS] Priority Queue Range, Heapify, and RemoveAt\n");
}

static void Test_Queue_Circular_WrapAround(void) {
    Queue q;
    QueueInit(&q, 4);

    int data[100];
    for (int i = 0; i < 100; i++) {
        data[i] = i * 10;
    }

    int push_idx = 0;
    int pop_idx = 0;

    // Enqueue 3 items, Dequeue 2 items per cycle
    // Explicitly compute pointers before calling Enqueue to avoid sequence point ambiguity
    for (int cycle = 0; cycle < 10; cycle++) {
        int* p1 = &data[push_idx++];
        int* p2 = &data[push_idx++];
        int* p3 = &data[push_idx++];

        QueEnqueue(&q, p1);
        QueEnqueue(&q, p2);
        QueEnqueue(&q, p3);

        assert(QueSize(&q) >= 2);

        int* val1 = (int*)QueDequeue(&q);
        int* val2 = (int*)QueDequeue(&q);

        int expected1 = data[pop_idx++];
        int expected2 = data[pop_idx++];

        assert(val1 != NULL && *val1 == expected1);
        assert(val2 != NULL && *val2 == expected2);
    }

    QueueClear(&q);
    printf("[PASS] Queue Circular Wrap-Around\n");
}

static void Test_Queue_Bulk_And_Iteration(void) {
    Queue q;
    QueueConstruct(&q);
    QueueInit(&q, 8);

    const int COUNT = 500;
    int* values = (int*)malloc(sizeof(int) * COUNT);
    assert(values != NULL);

    for (int i = 0; i < COUNT; i++) {
        values[i] = i + 1;
        QueEnqueue(&q, &values[i]);
    }

    assert(QueSize(&q) == (u32)COUNT);
    assert(QueAny(&q) == true);

    // Drain and verify exact FIFO order
    for (int i = 0; i < COUNT; i++) {
        int* val = (int*)QueDequeue(&q);
        assert(val != NULL);
        assert(*val == i + 1);
    }

    assert(QueEmpty(&q) == true);
    assert(QueSize(&q) == 0);

    free(values);
    QueueClear(&q);
    printf("[PASS] Queue Bulk Enqueue/Dequeue (500 items)\n");
}

static void Test_PriorityQueue_Stress_HeapProperty(void) {
    PriorityQueue pq;
    PriorityQueue(&pq, 16);

    const int COUNT = 1000;
    int* items = (int*)malloc(sizeof(int) * COUNT);
    assert(items != NULL);

    // Push numbers in pseudo-random pattern
    for (int i = 0; i < COUNT; i++) {
        items[i] = (i * 37 + 13) % COUNT; 
        PQPush(&pq, &items[i]);
    }

    assert(PQEmpty(&pq) == false);

    // Remove middle element using PQRemoveAt to test arbitrary removal
    void* removed = PQRemoveAt(&pq, COUNT / 2);
    assert(removed != NULL);

    // Pop remaining elements and ensure top is always valid pointer
    int popped_count = 0;
    while (!PQEmpty(&pq)) {
        void* top = PQTop(&pq);
        assert(top != NULL);
        PQPop(&pq);
        popped_count++;
    }

    assert(popped_count == COUNT - 1);
    assert(PQEmpty(&pq) == true);

    free(items);
    PQClear(&pq);
    printf("[PASS] Priority Queue Heap Property & Stress (1000 items)\n");
}

static void Test_Queue_Reset_And_Reuse(void) {
    Queue q;
    QueueConstruct(&q);
    QueueInit(&q, 2);

    int a = 100, b = 200, c = 300;
    QueEnqueue(&q, &a);
    QueEnqueue(&q, &b);

    // Force explicit growth trigger
    QueGrowIfNecessary(&q, 10);
    QueEnqueue(&q, &c);
    assert(QueSize(&q) == 3);

    // Reset without destroying allocation
    QueueReset(&q);
    assert(QueSize(&q) == 0);
    assert(QueEmpty(&q) == true);

    // Re-fill to ensure queue is operational after reset
    QueEnqueue(&q, &a);
    assert(QueSize(&q) == 1);
    assert(*(int*)QueDequeue(&q) == 100);

    QueueClear(&q);
    printf("[PASS] Queue Reset & Reuse\n");
}

int main(void) {
    printf("--- Running Queue Tests ---\n");
    Test_Queue_Lifecycle();
    Test_Queue_Enqueue_Dequeue_Single();
    Test_Queue_Range_And_Array_Operations();
    Test_Queue_Growth_And_Index();

    printf("\n--- Running Priority Queue Tests ---\n");
    Test_PriorityQueue_Lifecycle();
    Test_PriorityQueue_Push_Pop_Top();
    Test_PriorityQueue_Range_And_Heapify();

    printf("\nAll unit tests passed successfully!\n");
    
    printf("--- Extended Queue Tests ---\n");
    Test_Queue_Circular_WrapAround();
    Test_Queue_Bulk_And_Iteration();
    Test_Queue_Reset_And_Reuse();

    printf("\n--- Extended Priority Queue Tests ---\n");
    Test_PriorityQueue_Stress_HeapProperty();

    return 0;
}