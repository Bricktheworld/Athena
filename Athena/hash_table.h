#pragma once
#include "memory/memory.h"
#include "option.h"

u32 hash_u32(u32 val)
{
	return val;
}

u32 hash_u32(u64 val)
{
	return static_cast<u32>(val) ^ static_cast<u32>(val >> 32);
}

u32 hash_u32(void* ptr)
{
	return hash_u32(reinterpret_cast<u64>(ptr));
}

template <typename K, typename V>
struct HashTable
{
	struct Bucket
	{
		K key;
		V val;
	};

	Bucket* buckets = nullptr;
	u32* hashes     = nullptr;
	size_t capacity = 0;
	size_t size     = 0;
};

template <typename K, typename V>
inline HashTable<K, V>
init_hash_table(MEMORY_ARENA_PARAM, size_t capacity)
{
	HashTable<K, V> ret = {};

	ret.buckets  = push_memory_arena<HashTable<K, V>::Bucket>(capacity);
	ret.hashes   = push_memory_arena<u32>(capacity);
	ret.capacity = capacity;
	ret.size     = 0;

	return ret;
}

template <typename K, typename V>
inline V*
hash_table_insert(HashTable<K, V>* table, K key)
{
	u32 hash = hash_u32(key);
	u32 mask = table->capacity - 1;
}

template <typename K, typename V>
inline Option<V*>
hash_table_find(const HashTable<K, V>* table, K key)
{
	return None;
}


