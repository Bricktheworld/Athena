#include "memory.h"
#include <windows.h>

static void* g_memory_start = NULL;

enum struct MemoryLocation : u8
{
	GAME_MEM,
	GAME_VRAM,

	DEBUG_MEM,
};

//struct MemoryMapEntry
//{
//	MemoryLocation location = GAME_MEM;
//	size_t size = 0;
//};


struct DoubleEndedStack
{
	uintptr_t upper = 0x0;
	uintptr_t lower = 0x0;
};

DoubleEndedStack init_double_ended(void* start, size_t size)
{
	DoubleEndedStack res;
	res.lower = reinterpret_cast<uintptr_t>(start);
	res.upper = res.lower + size;
	return res;
}

//static const MemoryMapEntry MEMORY_MAP[] =
//{
//	{MemoryLocation::GAME_MEM, }
//};

// We'll just allocate a gig of memory LOL
static constexpr size_t HEAP_SIZE = GiB(1);

void init_memory_arena() 
{
	ASSERT(g_memory_start == NULL);
	g_memory_start = VirtualAlloc(0, HEAP_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void destroy_memory_arena()
{
	VirtualFree(g_memory_start, 0, MEM_RELEASE);
}

