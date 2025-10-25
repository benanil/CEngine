
#ifndef AX_OS_INCLUDED
#define AX_OS_INCLUDED

#include <stdint.h>

size_t OSGetPageSize(void);

size_t OSRoundToPage(size_t size);

void* OSAlloc(size_t size);

int OSFree(void *ptr, size_t size);

#endif