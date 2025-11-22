
#ifndef AX_OS_INCLUDED
#define AX_OS_INCLUDED

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif


size_t OSGetPageSize(void);

size_t OSRoundToPage(size_t size);

void* OSAlloc(size_t size);

int OSFree(void *ptr, size_t size);


#if defined(__cplusplus)
}
#endif

#endif