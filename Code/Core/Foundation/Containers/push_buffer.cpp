#include "push_buffer.h"

using Segment = PushBuffer::Segment;

PushBuffer
init_push_buffer(u64 segment_size, u64 commit_size, u64 reserve_size)
{
  ASSERT_MSG_FATAL(commit_size > 0, "Cannot initialize PushBuffer with commit size 0. Needs at least one segment committed.");
  commit_size                  = ALIGN_UP(commit_size,  segment_size);
  reserve_size                 = ALIGN_UP(reserve_size, segment_size);
  u64 commit_segment_count     = commit_size  / segment_size;
  u64 reserve_segment_count    = reserve_size / segment_size;

  u64 meta_commit_size         = commit_segment_count  * sizeof(Segment);
  u64 meta_reserve_size        = reserve_segment_count * sizeof(Segment);

  PushBuffer ret;
  ret.allocator                = init_stack_allocator(meta_commit_size + commit_size, meta_reserve_size + reserve_size);

  ret.segment_size             = segment_size;
  ret.commit_size              = commit_size;
  ret.commit_segments_start    = (Segment*)push_stack(&ret.allocator, meta_commit_size, alignof(Segment));
  ret.commit_segments_memory   = (uintptr_t)push_stack(&ret.allocator, commit_size, 1);
  ret.overflow_write_semaphore = 0;

  Segment* dst                 = ret.commit_segments_start;
  ret.write_head               = dst;
  ret.write_tail               = dst + commit_segment_count - 1;

  for (u32 isegment = 0; isegment < commit_segment_count; isegment++, dst++)
  {
    dst->base         = ret.commit_segments_memory + isegment * segment_size;
    dst->write        = dst->base;
    dst->read         = dst->base;
    dst->next         = dst == ret.write_tail ? nullptr : (dst + 1);
    dst->segment_size = ret.segment_size;
  }


  return ret;
}

// It is expected that the PushBuffer is locked here. This will push the segment into the read FIFO linked list.
static void
locked_push_buffer_flush_segment(PushBuffer* pb, Segment* segment)
{
  segment->next  = nullptr;

  if (pb->read_tail != nullptr)
  {
    ASSERT_MSG_FATAL(pb->read_tail->next == nullptr, "PushBuffer is in a bad state! read_tail->next is not nullptr! read_tail->next = 0x%llx", pb->read_tail->next);
    pb->read_tail->next = segment;
  }

  if (pb->read_head == nullptr)
  {
    ASSERT_MSG_FATAL(pb->read_tail == nullptr, "PushBuffer is in a bad state! read_head is nullptr but read_tail is 0x%llx. Something went wrong internally.", pb->read_tail);
    pb->read_head = pb->read_tail = segment;
  }
  else
  {
    pb->read_tail = segment;
  }
}

// It is expected that the PushBuffer is locked here. This will push the segment into the write FIFO linked list.
static void
locked_push_buffer_free_segment(PushBuffer* pb, Segment* segment)
{
  segment->next  = nullptr;
  segment->read  = segment->base;
  segment->write = segment->base;

  if (pb->allocator.commit_size > pb->allocator.initial_commit_size)
  {
    // TODO(bshihabi): We should handle decreasing the size of the push buffer once all of the relevant segments have been freed
  }

  if (pb->write_tail != nullptr)
  {
    ASSERT_MSG_FATAL(pb->write_tail->next == nullptr, "PushBuffer is in a bad state! write_tail->next is not nullptr! write_tail->next = 0x%llx", pb->write_tail->next);
    pb->write_tail->next = segment;
  }

  if (pb->write_head == nullptr)
  {
    ASSERT_MSG_FATAL(pb->write_tail == nullptr, "PushBuffer is in a bad state! write_head is nullptr but write_tail is 0x%llx. Something went wrong internally.", pb->write_tail);
    pb->write_head = pb->write_tail = segment;
  }
  else
  {
    pb->write_tail = segment;
  }
}

static void
locked_push_buffer_free_head(PushBuffer* pb)
{
  Segment* free_segment = pb->read_head;

  pb->read_head = pb->read_head->next;
  if (pb->read_head == nullptr)
  {
    ASSERT_MSG_FATAL(pb->read_tail == free_segment, "PushBuffer is in a bad state! The read_head had no following node but the read_tail does not match the read_head.");
    pb->read_tail = nullptr;
  }

  locked_push_buffer_free_segment(pb, free_segment);
}

u64
locked_push_buffer_flush_head(PushBuffer* pb)
{
  // Load the segment that we want to flush
  Segment* flush_segment = pb->write_head;

  // Don't flush empty segment
  if (flush_segment == nullptr)
  {
    return 0;
  }

  u64      size          = (u64)(flush_segment->write - flush_segment->base);

  // Don't flush an empty segment
  if (size == 0)
  {
    return 0;
  }

  pb->write_head = pb->write_head->next;
  if (pb->write_head == nullptr)
  {
    ASSERT_MSG_FATAL(pb->write_tail == flush_segment, "PushBuffer is in a bad state! The write_head had no following node but the write_tail does not match the write_head.");
    pb->write_tail = nullptr;
  }
  locked_push_buffer_flush_segment(pb, flush_segment);

  return size;
}

u64
push_buffer_flush(PushBuffer* pb)
{
  spin_acquire(&pb->lock);
  defer { spin_release(&pb->lock); };

  return locked_push_buffer_flush_head(pb);
}

static void*
alloc_from_segment(Segment* segment, u64 size)
{
  u64   available = segment->segment_size - (u64)(segment->write - segment->base);
  ASSERT_MSG_FATAL(available >= size, "Not enough space in segment to allocate %llu bytes from! Only %llu bytes available. This is a bug in the PushBuffer and this should've been handled further up the call stack.", size, available);

  void* ret       = (void*)segment->write;
  segment->write += size;

  return ret;
}

void*
push_buffer_begin_edit(PushBuffer* pb, u64 size)
{
  spin_acquire(&pb->lock);
  defer { spin_release(&pb->lock); };

  auto allocate_segment = [pb](u64 size) -> Segment*
  {
    ASSERT_MSG(false, "Overflowed push buffer! Allocated %llu bytes. This incurs a memory allocation which could be slow. Consider bumping this push buffer.", size);
    Segment* ret      = (Segment*)push_stack(&pb->allocator, sizeof(Segment), alignof(Segment));
    ret->base         = (uintptr_t)push_stack(&pb->allocator, size, 1);
    ret->write        = ret->base;
    ret->read         = ret->base;
    ret->next         = nullptr;
    ret->segment_size = size;

    return ret;
  };

  // If we overflow the entire segment size, then we need to allocate a bespoke segment
  if (size > pb->segment_size)
  {
    // This segment won't go into the regular queue because it's just specifically for this one fat allocation
    Segment* bespoke_segment = allocate_segment(size);

    void* dst = alloc_from_segment(bespoke_segment, size);
    locked_push_buffer_flush_segment(pb, bespoke_segment);

    return dst;
  }
  else
  {
    Segment* segment = pb->write_head;

    u64 remaining_space = segment ? (segment->segment_size - (u64)(segment->write - segment->base)) : 0;
    if (size > remaining_space)
    {
      // Flush the latest segment
      locked_push_buffer_flush_head(pb);
    }

    // Allocate a new segment if we need to
    if (pb->write_head == nullptr)
    {
      pb->write_head = pb->write_tail = allocate_segment(pb->segment_size);
    }

    // The previous operations modifying the head mean we need to get the write head again
    segment = pb->write_head;


    void* dst = alloc_from_segment(segment, size);

    uintptr_t segment_offset = (uintptr_t)dst - pb->commit_segments_memory;
    // Put a lock on writing until we're done writing from it
    if (segment_offset >= pb->commit_size)
    {
      pb->overflow_write_semaphore++;
    }
    else
    {
      segment->write_semaphore++;
    }

    return dst;
  }
}

void
push_buffer_end_edit(PushBuffer* pb, void* ptr)
{
  spin_acquire(&pb->lock);
  defer { spin_release(&pb->lock); };

  bool      is_pointer_in_range = (uintptr_t)ptr >= pb->commit_segments_memory && (uintptr_t)ptr < pb->allocator.pos;
  ASSERT_MSG_FATAL(is_pointer_in_range, "push_buffer_end_edit received pointer 0x%llx, but the memory range is only within 0x%llx - 0x%llx, are you passing the correct address? It should match with what push_buffer_begin_edit returns.", ptr, pb->commit_segments_memory, pb->allocator.pos);

  if (!is_pointer_in_range)
  {
    return;
  }

  uintptr_t segment_offset = (uintptr_t)ptr - pb->commit_segments_memory;
  // If it's not within the commit size, then it is in some overflow segment.
  if (segment_offset >= pb->commit_size)
  {
    ASSERT_MSG_FATAL(pb->overflow_write_semaphore > 0, "During push_buffer_end_edit on pointer 0x%llx, overflow_write_semaphore expected to be > 0 but is 0 in PushBuffer. This indicates that there is a mismatch push_buffer_begin_edit and push_buffer_end_edit somewhere.", ptr);
    if (pb->overflow_write_semaphore > 0)
    {
      pb->overflow_write_semaphore--;
    }
    return;
  }

  Segment* segment = pb->commit_segments_start + (u64)segment_offset / pb->segment_size;

  ASSERT_MSG_FATAL(segment->write_semaphore > 0, "During push_buffer_end_edit on pointer 0x%llx, attempting to decrease write semaphore on segment 0x%llx but write_semaphore = 0. This means there's a mismatch in the push/pop for the segment which indicates that there is an extra push_buffer_end_edit on a pointer in the range 0x%llx - 0x%llx", ptr, segment, segment->base, segment->base + segment->segment_size);
  if (segment->write_semaphore > 0)
  {
    segment->write_semaphore--;
  }
}

bool
try_push_buffer_pop(PushBuffer* pb, void* dst_base, u64 size)
{
  spin_acquire(&pb->lock);
  defer { spin_release(&pb->lock); };

  uintptr_t dst = (uintptr_t)dst_base;

  u32  segments_to_free     = 0;
  bool blocked_on_semaphore = false;
  auto read_segments = [&]()
  {
    for (Segment* isegment = pb->read_head; size > 0 && isegment != nullptr; isegment = isegment->next)
    {
      ASSERT_MSG_FATAL(isegment->read <= isegment->write, "PushBuffer is in a bad state! The read should never advance further than the write pointer for segment 0x%llx.", isegment);

      // This means that someone is actively writing into the segment. It's not ready to consume so we can't actually use it yet.
      if (isegment->write_semaphore != 0)
      {
        blocked_on_semaphore = true;
        break;
      }

      // This means that someone is actively writing into an overflow segment which could possibly be isegment since it is an overflow segment. This segment is possibly not ready to consume.
      if (isegment->base > pb->commit_segments_memory + pb->commit_size && pb->overflow_write_semaphore > 0)
      {
        blocked_on_semaphore = true;
        break;
      }

      u64 unread_bytes = (u64)(isegment->write - isegment->read);
      u64 read_bytes   = MIN(unread_bytes, size);

      // It is permitted to not specify any buffer to pop from, in which case we will just skip the memcpy
      if (dst_base != nullptr)
      {
        memcpy((void*)dst, (const void*)isegment->read, read_bytes);
      }

      size            -= read_bytes;
      isegment->read  += read_bytes;

      ASSERT_MSG_FATAL(isegment->read <= isegment->write, "PushBuffer is in a bad state! The read should never advance further than the write pointer for segment 0x%llx.", isegment);
      if (isegment->read < isegment->write)
      {
        ASSERT_MSG_FATAL(size == 0, "There are still %llu bytes to read in segment 0x%llx, but size was not filled. This is a bug in the PushBuffer.", (u64)(isegment->write - isegment->read), isegment);
      }
      else
      {
        segments_to_free++;
      }
    }
  };


  // Not enough bytes have been flushed!
  while (size > 0)
  {
    read_segments();

    // If we're blocked on a semaphore, flushing more segments isn't going to do anything, so we should just return
    if (blocked_on_semaphore)
    {
      return false;
    }

    // Do a flush now and try again, just in case 

    // NOTE(bshihabi): We flush here in the case that the consumer isn't flushing often enough. We don't do it earlier because then the consumer could be too eager and that would
    // defeat the purpose of having segments in the first place. So we flush only once we know there is not enough contiguous data.
    u64 bytes_flushed = locked_push_buffer_flush_head(pb);

    // If not enough bytes were flushed still, then we can bail completely. That makes the flush somewhat of a waste, but that is okay, it means we still need to feed the consumer anyway.
    if (bytes_flushed < size)
    {
      return false;
    }
  }

  while (segments_to_free > 0)
  {
    // Keep freeing the head
    locked_push_buffer_free_head(pb);
    segments_to_free--;
  }

  return true;
}

void
push_buffer_pop(PushBuffer* pb, void* dst, u64 size)
{
  if (size == 0)
  {
    return;
  }

  bool ok = try_push_buffer_pop(pb, dst, size);
  ASSERT_MSG_FATAL(ok, "Failed to pop from PushBuffer! This likely means that the allocator entirely ran out of both reserve and commit memory. It is recommended to figure out why this occured and also bump the reserve memory so that a crash doesn't occur.");
}
