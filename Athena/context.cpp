#include "context.h"

thread_local Context tls_ctx = {0};

#define CTX_IS_INITIALIZED (tls_ctx.scratch_arena.start != 0x0)
#define ASSERT_CTX_INIT() ASSERT(CTX_IS_INITIALIZED)

static void
reserve_prev_ctx_storage(MemoryArena* arena)
{
  ASSERT(arena->size > sizeof(Context));
  arena->size -= sizeof(Context);
  arena->pos += sizeof(Context);
  arena->start += sizeof(Context);
}

static Context*
get_prev_storage(MemoryArena* arena)
{
  return reinterpret_cast<Context*>(arena->start - sizeof(Context));
}

Context
init_context(MemoryArena arena)
{
  Context ret = {0};
  reserve_prev_ctx_storage(&arena);
  ret.scratch_arena = arena;
  ret.prev = nullptr;

  if (!CTX_IS_INITIALIZED)
  {
    tls_ctx = ret;
  }

  return ret;
}

void
push_context(Context ctx)
{
  ASSERT_CTX_INIT();

  Context cur = tls_ctx;
  tls_ctx = ctx;

  // We need a place to put the old context, so we're gonna do that here.
  tls_ctx.prev = get_prev_storage(&tls_ctx.scratch_arena);
  *tls_ctx.prev = cur;

  ASSERT(tls_ctx.scratch_arena.start != 0);
  ASSERT(tls_ctx.prev->scratch_arena.start != 0);
}

Context
pop_context()
{
  ASSERT_CTX_INIT();

  Context* prev = tls_ctx.prev;
  ASSERT(prev != nullptr);
  ASSERT(tls_ctx.prev->scratch_arena.start != 0);

  Context ret = tls_ctx;
  tls_ctx = *prev;

  uintptr_t prev_pos = reinterpret_cast<uintptr_t>(prev);

  // Here we essentially just pop the context data that we previously pushed
  // onto the memory arena.
  tls_ctx.scratch_arena.pos = prev_pos;

  ASSERT(tls_ctx.scratch_arena.start != 0);

  return ret;
}

uintptr_t*
context_get_scratch_arena_pos_ptr()
{
  return &tls_ctx.scratch_arena.pos;
}

MemoryArena
alloc_scratch_arena()
{
  ASSERT_CTX_INIT();

  MemoryArena ret = {0};
  ret.use_ctx_pos = true;
  ret.start = *memory_arena_pos_ptr(&ret);

  ASSERT(tls_ctx.scratch_arena.pos >= tls_ctx.scratch_arena.start);

  size_t diff = static_cast<size_t>(*memory_arena_pos_ptr(&tls_ctx.scratch_arena) - tls_ctx.scratch_arena.start);
  ASSERT(tls_ctx.scratch_arena.size >= diff);

  ret.size = tls_ctx.scratch_arena.size - diff;

  ASSERT(ret.size > 0);

  return ret;
}

void
free_scratch_arena(MEMORY_ARENA_PARAM)
{
  ASSERT_CTX_INIT();

  reset_memory_arena(MEMORY_ARENA_FWD);
}