#include "ring_buffer.h"

RingBuffer
init_ring_buffer(MEMORY_ARENA_PARAM, size_t alignment, size_t size)
{
  RingBuffer ret = {0};
  if (size == 0)
  {
    size = memory_arena->size;
    ASSERT(size != 0);
  }

  ret.buffer = static_cast<byte*>(push_memory_arena_aligned(MEMORY_ARENA_FWD, size, alignment));
  ret.size = size;
  ret.write = 0;
  ret.read = 0;
  ret.watermark = size;

  return ret;
}

check_return bool
try_ring_buffer_push(RingBuffer* rb, const void* data, size_t size)
{
  size_t watermark = rb->watermark;
  size_t write = rb->write;
  if (write >= rb->read)
  {
    ASSERT(watermark == rb->size);
    // See comment below about why we are not using >=
    if (rb->size - write > size)
    {
      memcpy(rb->buffer + rb->write, data, size);
      rb->write += size;
      return true;
    }

    watermark = write;
    write = 0;
  }

  // TODO(Brandon): TECHNICALLY the fact that we are using <= instead of <
  // means that it is not possible to take full advantage of the ring buffer
  // because you are not able to completely fill the buffer, there has to be
  // _at least_ one additional byte between the read and write otherwise
  // this implementation will view the ring buffer as empty. This should
  // definitely be fixed...
  if (rb->read - write <= size)
    return false;

  memcpy(rb->buffer + write, data, size);
  write += size;

  rb->watermark = watermark;
  rb->write = write;

  return true;
}

check_return bool
try_ring_buffer_pop(RingBuffer* rb, size_t size, void* out)
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
  rb->read = read + size;
  rb->watermark = watermark;

  if (out != nullptr)
  {
    memcpy(out, cpy_addr, size);
  }

  return true;
}

void
ring_buffer_push(RingBuffer* rb, const void* data, size_t size)
{
  ASSERT(try_ring_buffer_push(rb, data, size));
}

void
ring_buffer_pop(RingBuffer* rb, size_t size, void* out)
{
  ASSERT(try_ring_buffer_pop(rb, size, out));
}

//bool ring_buffer_is_full(const RingBuffer& rb)
//{
//  return (rb.write >= rb.read ? rb.write - rb.read : rb.read - rb.write) == 1;
//}

bool ring_buffer_is_empty(const RingBuffer& rb)
{
  return rb.read == rb.write;
}
