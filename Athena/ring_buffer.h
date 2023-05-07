#pragma once
#include "memory/memory.h"

struct RingBuffer
{
	byte* buffer = nullptr;
	size_t size = 0;
	size_t write = 0;
	size_t read = 0;
	size_t watermark = 0;
};

// If the ring buffer is passed a size of 0, then it will fill
// up the entire memory arena.
// NOTE(Brandon): The size of this ring buffer is actually size - 1, so the maximum,
// number of bytes will always be size - 1
RingBuffer init_ring_buffer(MEMORY_ARENA_PARAM, size_t alignment, size_t size = 0);

check_return bool try_ring_buffer_push(RingBuffer* rb, const void* data, size_t size);
void ring_buffer_push(RingBuffer* rb, const void* data, size_t size);
check_return bool try_ring_buffer_pop(RingBuffer* rb, size_t size, void* out = nullptr);
void ring_buffer_pop(RingBuffer* rb, size_t size, void* out = nullptr);

//bool ring_buffer_is_full(const RingBuffer& rb);
bool ring_buffer_is_empty(const RingBuffer& rb);

