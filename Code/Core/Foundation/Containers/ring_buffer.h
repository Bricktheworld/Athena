#pragma once
#include "Core/Foundation/memory.h"

struct RingBuffer
{
  u8*    buffer    = nullptr;
  size_t size      = 0;
  size_t write     = 0;
  size_t read      = 0;
  size_t watermark = 0;
};

// If the ring buffer is passed a size of 0, then it will fill
// up the entire memory arena.
// NOTE(Brandon): The size of this ring buffer is actually size - 1, so the maximum,
// number of bytes will always be size - 1
FOUNDATION_API RingBuffer init_ring_buffer(AllocHeap heap, size_t alignment, size_t size = 0);

FOUNDATION_API DONT_IGNORE_RETURN bool try_ring_buffer_push(RingBuffer* rb, const void* data, size_t size);
FOUNDATION_API void ring_buffer_push(RingBuffer* rb, const void* data, size_t size);
FOUNDATION_API DONT_IGNORE_RETURN bool try_ring_buffer_pop(RingBuffer* rb, size_t size, void* out = nullptr);
FOUNDATION_API void ring_buffer_pop(RingBuffer* rb, size_t size, void* out = nullptr);

//bool ring_buffer_is_full(const RingBuffer& rb);
FOUNDATION_API bool ring_buffer_is_empty(const RingBuffer& rb);

template <typename T>
struct RingQueue
{
  RingBuffer buffer;
};

template <typename T>
inline RingQueue<T>
init_ring_queue(AllocHeap heap, size_t size)
{
  ASSERT(size > 0);
  // TODO(Brandon): ??? Shouldn't it just be 1?
  size += 2;

  RingQueue<T> ret = {0};
  ret.buffer = init_ring_buffer(heap, alignof(T), sizeof(T) * size);

  return ret;
}

template <typename T>
inline DONT_IGNORE_RETURN bool
try_ring_queue_push(RingQueue<T>* queue, const T& data)
{
  return try_ring_buffer_push(&queue->buffer, &data, sizeof(data));
}

template <typename T>
inline void
ring_queue_push(RingQueue<T>* queue, const T& data)
{
  ring_buffer_push(&queue->buffer, &data, sizeof(data));
}

template <typename T>
inline DONT_IGNORE_RETURN bool
try_ring_queue_pop(RingQueue<T>* queue, T* out = nullptr)
{
  return try_ring_buffer_pop(&queue->buffer, sizeof(T), out);
}

template <typename T>
inline void
ring_queue_pop(RingQueue<T>* queue, T* out = nullptr)
{
  ring_buffer_pop(&queue->buffer, sizeof(T), out);
}

template <typename T>
inline bool
ring_queue_is_empty(const RingQueue<T>& queue)
{
  return ring_buffer_is_empty(queue.buffer);
}

template <typename T>
inline void
ring_queue_peak_front(const RingQueue<T>& queue, T* out = nullptr)
{
  ASSERT(!ring_queue_is_empty(queue));
  memcpy(out, queue.buffer.buffer + queue.buffer.read, sizeof(T));
}
