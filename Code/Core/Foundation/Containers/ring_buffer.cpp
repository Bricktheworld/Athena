#include "Core/Foundation/threading.h"

#include "Core/Foundation/Containers/ring_buffer.h"

RingBuffer
init_ring_buffer(AllocHeap heap, size_t alignment, size_t size)
{
  RingBuffer ret = {0};
  ASSERT(size != 0);

  ret.buffer    = HEAP_ALLOC_ALIGNED(heap, size, alignment);
  ret.size      = size;
  ret.write     = 0;
  ret.read      = 0;
  ret.watermark = size;

  return ret;
}

void*
try_ring_buffer_push(RingBuffer* rb, size_t size)
{
  size_t watermark = rb->watermark;
  size_t write     = rb->write;
  if (write >= rb->read)
  {
    ASSERT(watermark == rb->size);
    // See comment below about why we are not using >=
    if (rb->size - write > size)
    {
      void* ret  = rb->buffer + rb->write;
      // memcpy(, data, size);
      rb->write += size;
      return ret;
    }

    watermark = write;
    write     = 0;
  }

  // TODO(Brandon): TECHNICALLY the fact that we are using <= instead of <
  // means that it is not possible to take full advantage of the ring buffer
  // because you are not able to completely fill the buffer, there has to be
  // _at least_ one additional byte between the read and write otherwise
  // this implementation will view the ring buffer as empty. This should
  // definitely be fixed...
  if (rb->read - write <= size)
  {
    return nullptr;
  }

  void* ret = rb->buffer + write;
  // memcpy(, data, size);
  write += size;

  rb->watermark = watermark;
  rb->write = write;

  return ret;
}

const void*
try_ring_buffer_peak(const RingBuffer& rb, size_t size)
{
  size_t read      = rb.read;
  size_t watermark = rb.watermark;

  if (rb.write >= read)
  {
    if (rb.write - read < size)
      return nullptr;

  }
  else // (rb->write < read)
  {
    if (read >= watermark)
    {
      read      = 0;
      watermark = rb.size;

      if (rb.write < size)
        return nullptr;
    }
    // TODO(Brandon): Allow reading _through_ the watermark by skipping over it.
    else if (watermark - read < size)
    {
      return nullptr;
    }
  }

  return rb.buffer + rb.read;
}

DONT_IGNORE_RETURN bool
try_ring_buffer_pop(RingBuffer* rb, void* dst, size_t size)
{
  size_t read = rb->read;
  size_t watermark = rb->watermark;

  if (rb->write >= read)
  {
    if (rb->write - read < size)
      return false;

  }
  else // (rb->write < read)
  {
    if (read >= watermark)
    {
      read = 0;
      watermark = rb->size;

      if (rb->write < size)
        return false;
    }
    // TODO(Brandon): Allow reading _through_ the watermark by skipping over it.
    else if (watermark - read < size)
    {
      return false;
    }

  }

  void* cpy_addr = rb->buffer + read;
  rb->read       = read + size;
  rb->watermark  = watermark;

  if (dst != nullptr)
  {
    memcpy(dst, cpy_addr, size);
  }

  return true;
}

void*
ring_buffer_push(RingBuffer* rb, size_t size)
{
  // Busy wait
  void* dst = try_ring_buffer_push(rb, size);
  ASSERT_MSG_FATAL(dst != nullptr, "Failed to push to ring buffer! Maybe it's full?");
  return dst;
}

void
ring_buffer_pop(RingBuffer* rb, void* dst, size_t size)
{
  bool res = try_ring_buffer_pop(rb, dst, size);
  ASSERT_MSG_FATAL(res, "Failed to pop from ring buffer! Maybe it's empty?");
}

//bool ring_buffer_is_full(const RingBuffer& rb)
//{
//  return (rb.write >= rb.read ? rb.write - rb.read : rb.read - rb.write) == 1;
//}

bool ring_buffer_is_empty(const RingBuffer& rb)
{
  return rb.read == rb.write;
}
