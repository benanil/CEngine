#ifndef ARENA_H_
#define ARENA_H_

#include "Memory.h"
#include <stddef.h>

typedef struct Arena_ {
	char *buf;
	size_t         buf_len;
	size_t         curr_offset;
} Arena;

static inline void arena_init(Arena *a, size_t backing_buffer_length) {
	size_t aligned_size = OSRoundToPage(backing_buffer_length);
    a->buf = (char*)OSAlloc(aligned_size);
	a->buf_len = aligned_size;
	a->curr_offset = 0;
}

static inline void *arena_alloc_align(Arena *a, size_t size, size_t align) {
	size_t curr_ptr = (size_t)a->buf + a->curr_offset;
	size_t offset = AlignAddress(curr_ptr, align);
	offset -= (size_t)a->buf; // Change to relative offset

	ASSERT(offset + size <= a->buf_len);
	void *ptr = &a->buf[offset];
	a->curr_offset = offset + size;
	return ptr;
}

static inline void *arena_alloc(Arena *a, size_t size) {
	return arena_alloc_align(a, size, 2 * sizeof(void*)); // default alignment
}

static inline void arena_destroy(Arena* a)
{
	OSFree(a->buf, a->buf_len);
}

#define ARENA_STRUCT(arena, type)     (arena_alloc_align(arena, sizeof(type), ALIGNOF(type)))
#define ARENA_ARRAY(arena, type, cnt) (arena_alloc_align(arena, sizeof(type) * cnt, ALIGNOF(type)))


#endif //ARENA_H_