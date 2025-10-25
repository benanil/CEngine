
#include "Include/Common.h"

#ifndef PLATFORM_WINDOWS
    #include <sys/mman.h>
    #include <unistd.h>
    #include <errno.h>
#else
    #ifndef NOMINMAX
    #  define NOMINMAX
    #  define WIN32_LEAN_AND_MEAN 
    #  define VC_EXTRALEAN
    #endif
    #include <Windows.h>
#endif

size_t OSGetPageSize(void) {
    #ifdef PLATFORM_WINDOWS
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwPageSize;
    #else
    return (size_t)getpagesize();
    #endif
}

size_t OSRoundToPage(size_t size) {
    size_t page_size = OSGetPageSize();
    return (size + page_size - 1) & ~(page_size - 1);
}

void* OSAlloc(size_t size) 
{
    if (size == 0) {
        return NULL;
    }
    
    #ifdef PLATFORM_WINDOWS
    DWORD mask = MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES; 
    void *ptr = VirtualAlloc(NULL, size, mask, PAGE_READWRITE);
    #else
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    #endif
    return ptr;
}

int OSFree(void *ptr, size_t size) {
    if (ptr == NULL) {
        return -1;
    }
    
    #ifdef PLATFORM_WINDOWS
    (void)size; /* Windows doesn't need size for VirtualFree */
    if (VirtualFree(ptr, 0, MEM_RELEASE))  return 0;
    else return (int)GetLastError();
    #else
    if (munmap(ptr, size) == 0) return 0;
    else return errno;
    #endif
}