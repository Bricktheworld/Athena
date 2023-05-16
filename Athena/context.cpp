#include "context.h"

thread_local Context tls_ctx = {0};

#define CTX_IS_INITIALIZED (tls_ctx.scratch_arena.start != 0x0)
#define ASSERT_CTX_INIT() ASSERT(CTX_IS_INITIALIZED)

Context
init_context(MemoryArena arena)
{
	Context ret = {0};
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
	tls_ctx.prev = push_memory_arena<Context>(&tls_ctx.scratch_arena);
	*tls_ctx.prev = cur;

	// This is just a sanity check to make sure that our rather dangerous strategy
	// of holding a pointer to the pos value in context works even though we're moving
	// the data around technically. It works because tls_ctx should stay in the same
	// memory location always.
	ASSERT(cur.scratch_arena.remote_pos == tls_ctx.scratch_arena.remote_pos);
}

Context
pop_context()
{
	ASSERT_CTX_INIT();

	Context* prev = tls_ctx.prev;
	ASSERT(prev != nullptr);

	Context ret = tls_ctx;
	tls_ctx = *prev;

	uintptr_t prev_pos = reinterpret_cast<uintptr_t>(prev);
//	ASSERT(prev_pos < *tls_ctx.scratch_arena.pos && prev_pos >= tls_ctx.scratch_arena.start);

	// Here we essentially just pop the context data that we previously pushed
	// onto the memory arena.
	*memory_arena_pos_ptr(&tls_ctx.scratch_arena) = prev_pos;

	return ret;
}

MemoryArena
alloc_scratch_arena()
{
	ASSERT_CTX_INIT();

	MemoryArena ret = {0};
	ASSERT(tls_ctx.scratch_arena.remote_pos || tls_ctx.prev == nullptr);
	ret.remote_pos = memory_arena_pos_ptr(&tls_ctx.scratch_arena);
	ret.start = *ret.remote_pos;

	ASSERT(*memory_arena_pos_ptr(&tls_ctx.scratch_arena) >= tls_ctx.scratch_arena.start);

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