#pragma once
#include "memory/memory.h"
#include "option.h"
#include "iterator.h"

template <typename T>
struct Array
{
	T* memory = nullptr;
	size_t size = 0;
	size_t capacity = 0;

	const T& operator[](size_t index) const
	{
		ASSERT(memory != nullptr && index < size);
		return memory[index];
	}

	T& operator[](size_t index)
	{
		ASSERT(memory != nullptr && index < size);
		return memory[index];
	}

	USE_ITERATOR(Array, T)
};

template <typename T>
inline Array<T>
init_array(MEMORY_ARENA_PARAM, size_t capacity)
{
	Array<T> ret = {0};
	ret.memory = push_memory_arena<T>(MEMORY_ARENA_FWD, capacity);
	ret.capacity = capacity;
	ret.size = 0;
	return ret;
}

template <typename T>
inline T*
array_add(Array<T>* arr)
{
	ASSERT(arr->memory != nullptr && arr->size < arr->capacity);

	T* ret =  &arr->memory[arr->size++];
	zero_memory(ret, sizeof(T));
	return ret;
}

template <typename T>
inline T*
array_insert(Array<T>* arr, size_t index)
{
	ASSERT(arr->memory != nullptr && arr->size < arr->capacity && index < arr->size);

	memmove(arr->memory + index + 1, arr->memory + index, (arr->size - index) * sizeof(T));

	arr->size++;
	T* ret = &arr->memory[index];
	zero_memory(ret, sizeof(T));

	return ret;
}

template <typename T>
inline void
array_remove_last(Array<T>* arr)
{
	ASSERT(arr->memory != nullptr && arr->size > 0);

	arr->size--;
}

// NOTE(Brandon): This is an unordered remove.
template <typename T>
inline void
array_remove(Array<T>* arr, size_t index)
{
	ASSERT(arr->memory != nullptr && arr->size < arr->capacity && index < arr->size);

	arr->size--;
	arr->memory[index] = arr->memory[arr->size];
}

template <typename T, typename F>
inline Option<size_t>
array_find_predicate(Array<T>* arr, F predicate)
{
	for (u32 i = 0; i < arr->size; i++)
	{
		if (predicate(&arr->memory[i]))
			return i;
	}

	return None;
}

template <typename T, typename F>
inline Option<T*>
array_find_value_predicate(Array<T>* arr, F predicate)
{
	for (u32 i = 0; i < arr->size; i++)
	{
		if (predicate(&arr->memory[i]))
			return &arr->memory[i];
	}

	return None;
}

template <typename T>
inline T*
array_at(Array<T>* arr, size_t index)
{
	ASSERT(arr->memory != nullptr && index < arr->size);
	return arr->memory + index;
}

#define array_find(arr, pred) array_find_predicate(arr, [&](auto* it) { return pred; })
#define array_find_value(arr, pred) array_find_value_predicate(arr, [&](auto* it) { return pred; })
//#define array_find_value(arr, pred) array_at(arr, array_find(arr, pred))
