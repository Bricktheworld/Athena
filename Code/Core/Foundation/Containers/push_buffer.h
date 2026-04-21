#pragma once

#include "Core/Foundation/memory.h"
#include "Core/Foundation/threading.h"

// This is sorta like a ring buffer but it's specifically designed to handle overflows, batch submission, and work with multi-threading
// This is best for command memory/scratch memory that is allowed to overflow and shouldn't block. It is intentionally designed similarly to how a driver or library would allocate
// command buffers, as that pattern is pretty common.
struct PushBuffer
{
  
  struct Segment
  {
    uintptr_t base            = 0;
    uintptr_t write           = 0;
    uintptr_t read            = 0;
    Segment*  next            = nullptr;
    u64       segment_size    = 0;
    u64       write_semaphore = 0;
  };

  // Global lock because I haven't written anything to be more fine grained. I suspect that there might be contention wayyy down the line
  // so it feels like it's possible to write this data structure to be lock-free which would be nice.
  SpinLock        lock;

  // Backing allocator for the push buffer (also supports overflows)
  StackAllocator  allocator;

  // Size of individual segments that are batch flushed
  u64             segment_size             = 0;

  // FIFO queue feeding segments for the write end of the ring buffer
  Segment*        write_head               = nullptr;
  Segment*        write_tail               = nullptr;

  // FIFO queue feeding segments for the read end of the ring buffer
  Segment*        read_head                = nullptr;
  Segment*        read_tail                = nullptr;

  // Pointers to segment metadata to find segment given pointer. Commit segments are pointers to segments that are always resident in memory
  Segment*        commit_segments_start    = nullptr;

  // Pointer to the actual memory for committed segments
  uintptr_t       commit_segments_memory   = 0;
  u64             commit_size              = 0;

  // When overflow segments are being written to, overflow_write_semaphore > 0 which means that you're not allowed to consume any segments that are marked as overflow.
  // This is to work around the fact that there is no nice way for me to currently go from pointer -> overflow segment, so a global write semaphore is fastest.
  u64             overflow_write_semaphore = 0;
};


// Internal segments get flushed automatically when they fill up. You can also manually initiate a flush which is a good idea. Reads will only happen from flushed segments 
FOUNDATION_API                                PushBuffer init_push_buffer   (u64 segment_size, u64 commit_size, u64 reserve_size);
FOUNDATION_API THREAD_SAFE                    u64        push_buffer_flush  (PushBuffer* pb);  // Returns the number of bytes that were flushed
// Returns a pointer to contiguous buffer of size specified. This must be closed with the push_buffer_end_edit to ensure that the segment is allowed to flush.
FOUNDATION_API THREAD_SAFE                    void*      push_buffer_begin_edit(PushBuffer* pb, u64 size);
FOUNDATION_API THREAD_SAFE                    void       push_buffer_end_edit  (PushBuffer* pb, void* ptr);
inline THREAD_SAFE void
push_buffer_push(PushBuffer* pb, const void* src, u64 size)
{
  void* dst = push_buffer_begin_edit(pb, size);
  defer { push_buffer_end_edit(pb, dst); };

  memcpy(dst, src, size);
}
// NOTE(bshihabi): There is intentionally no peak functionality, because if you choose to peak data that is non-contiguous, there is no nice way of returning a pointer. I am still deciding if
// I should just handle that case by returning the amount actually "peakable", assert, or something else. Still deciding...
FOUNDATION_API THREAD_SAFE DONT_IGNORE_RETURN bool       try_push_buffer_pop(PushBuffer* pb, void* dst, u64 size);
FOUNDATION_API THREAD_SAFE                    void       push_buffer_pop    (PushBuffer* pb, void* dst, u64 size);

inline THREAD_SAFE void
push_buffer_pop(PushBuffer* allocator, u64 size)
{
  push_buffer_pop(allocator, nullptr, size);
}

