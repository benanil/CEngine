#ifndef PARALLEL_FOR_H
#define PARALLEL_FOR_H

#include "Common.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef void (*ParallelForFn)(u32 begin, u32 end, void* userData);

// Runs fn on item ranges [begin, end). Small workloads run serially.
void ParallelFor(u32 itemCount, u32 minItemsPerWorker, ParallelForFn fn, void* userData);

#if defined(__cplusplus)
}
#endif

#endif // PARALLEL_FOR_H
