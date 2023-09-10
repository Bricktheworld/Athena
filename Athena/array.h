#pragma once
#include "memory/memory.h"
#include "option.h"
#include "iterator.h"

template <typename T>
struct Span
{
  const T* const memory = nullptr;
  const size_t size = 0;

  Span() = default;
  Span(const T* memory, size_t size) : memory(memory), size(size) {}
  Span(InitializerList<T> initializer_list) : memory(initializer_list.begin()), size(initializer_list.size()) {}

  template <size_t input_size>
  Span(const T(&arr)[input_size]) : memory(arr), size(input_size) {}

  const T& operator[](size_t index) const
  {
    ASSERT(memory != nullptr && index < size);
    return memory[index];
  }

  USE_CONST_ITERATOR(Span, T)
};

template <typename T, size_t static_size = 0>
struct Array
{
  T memory[static_size]{0};
  size_t size = 0;
  const size_t capacity = static_size;

  Array() = default;

  template <size_t input_size>
  Array(const T(&arr)[input_size])
  {
    static_assert(input_size <= static_size);
    size = input_size;
    memcpy(memory, arr, sizeof(T) * input_size);
  }

  template <size_t input_size>
  Array& operator=(const T(&arr)[input_size])
  {
    static_assert(input_size <= static_size);
    size = input_size;
    memcpy(memory, arr, sizeof(T) * input_size);
    return *this;
  }

  Array(const Span<T>& span)
  { 
    ASSERT(span.size <= capacity);
    size = span.size;
    memcpy(memory, span.memory, sizeof(T) * span.size);
  }

  Array& operator=(const Span<T>& span)
  { 
    ASSERT(span.size <= capacity);
    size = span.size;
    memcpy(memory, span.memory, sizeof(T) * span.size);
    return *this;
  }

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

  operator Span<T>()
  {
    return Span<T>(memory, size);
  }

  USE_ITERATOR(Array, T)
};

template <typename T>
struct Array<typename T, 0>
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

  operator Span<T>()
  {
    return Span<T>(memory, size);
  }

  USE_ITERATOR(Array, T)
};

template <typename T>
inline Array<T, 0>
init_array(MEMORY_ARENA_PARAM, size_t capacity)
{
  Array<T, 0> ret = {0};
  ret.memory = push_memory_arena<T>(MEMORY_ARENA_FWD, capacity);
  ret.capacity = capacity;
  ret.size = 0;
  return ret;
}


template <typename T, size_t S>
inline T*
array_add(Array<T, S>* arr)
{
  ASSERT(arr->memory != nullptr && arr->size < MAX(arr->capacity, S));

  T* ret =  &arr->memory[arr->size++];
  zero_memory(ret, sizeof(T));
  return ret;
}

template <typename T, size_t S>
inline T*
array_insert(Array<T, S>* arr, size_t index)
{
  ASSERT(arr->memory != nullptr && arr->size < MAX(arr->capacity, S) && index < arr->size);

  memmove(arr->memory + index + 1, arr->memory + index, (arr->size - index) * sizeof(T));

  arr->size++;
  T* ret = &arr->memory[index];
  zero_memory(ret, sizeof(T));

  return ret;
}

template <typename T, size_t S>
inline void
array_remove_last(Array<T, S>* arr)
{
  ASSERT(arr->memory != nullptr && arr->size > 0);

  arr->size--;
}

// NOTE(Brandon): This is an unordered remove.
template <typename T, size_t S>
inline void
array_remove(Array<T, S>* arr, size_t index)
{
  ASSERT(arr->memory != nullptr && arr->size < MAX(arr->capacity, S) && index < arr->size);

  arr->size--;
  arr->memory[index] = arr->memory[arr->size];
}

template <typename T, typename F, size_t S>
inline Option<size_t>
array_find_predicate(Array<T, S>* arr, F predicate)
{
  ASSERT(arr->memory != nullptr);
  for (u32 i = 0; i < arr->size; i++)
  {
    if (predicate(&arr->memory[i]))
      return i;
  }

  return None;
}

template <typename T, typename F, size_t S>
inline Option<T*>
array_find_value_predicate(Array<T, S>* arr, F predicate)
{
  ASSERT(arr->memory != nullptr);
  for (u32 i = 0; i < arr->size; i++)
  {
    if (predicate(&arr->memory[i]))
      return &arr->memory[i];
  }

  return None;
}

template <typename T, size_t S>
inline T*
array_at(Array<T, S>* arr, size_t index)
{
  ASSERT(arr->memory != nullptr && index < arr->size);
  return arr->memory + index;
}

template <typename T, size_t S>
inline void
array_copy(Array<T, S>* dst, const Array<T>& src)
{
  ASSERT(src.memory != nullptr && dst->memory != nullptr);
  ASSERT(dst->size == 0);
  ASSERT(MAX(dst->capacity, S) >= src.size);

  memcpy(dst->memory, src.memory, sizeof(T) * src.size);
  dst->size = src.size;
}

template <typename T, size_t S>
inline void
array_copy(Array<T, S>* dst, const Span<T>& src)
{
  ASSERT(src.memory != nullptr && dst->memory != nullptr);
  ASSERT(dst->size == 0);
  ASSERT(MAX(dst->capacity, S) >= src.size);

  memcpy(dst->memory, src.memory, sizeof(T) * src.size);
  dst->size = src.size;
}

template <typename T>
inline Array<T>
init_array(MEMORY_ARENA_PARAM, const Span<T>& src)
{
  ASSERT(src.memory != nullptr);
  auto ret = init_array<T>(MEMORY_ARENA_FWD, src.size);
  array_copy(&ret, src);
  return ret;
}

#define array_find(arr, pred) array_find_predicate(arr, [&](auto* it) { return pred; })
#define array_find_value(arr, pred) array_find_value_predicate(arr, [&](auto* it) { return pred; })

template <typename T, size_t S>
inline void
zero_array(Array<T, S>* arr, size_t size)
{
  ASSERT(arr->memory != nullptr);
  ASSERT(size <= MAX(arr->capacity, S));

  arr->size = size;

  if (size > 0)
  {
    zero_memory(arr->memory, sizeof(T) * size);
  }
}

template <typename T, size_t S>
inline void
clear_array(Array<T, S>* arr)
{
  ASSERT(arr->memory != nullptr);

//  zero_memory(arr->memory, sizeof(T) * arr->size);
  arr->size = 0;
}

template <typename T, size_t S>
inline void
zero_array(Array<T, S>* arr)
{
  ASSERT(arr->memory != nullptr);
  zero_array(arr, MAX(arr->capacity, S));
}

template <typename T, size_t S>
inline void
resize_array(Array<T, S>* arr, size_t size, const T& value)
{
  ASSERT(arr->memory != nullptr);
  ASSERT(size <= MAX(arr->capacity, S));
  arr->size = size;

  for (size_t i = 0; i < size; i++)
  {
    memcpy(arr->memory + i, &value, sizeof(T));
  }
}

template <typename T, size_t S>
inline void
resize_array(Array<T, S>* arr, const T& value)
{
  ASSERT(arr->memory != nullptr);
  resize_array(arr, MAX(arr->capacity, S), value);
}

template <typename T, size_t S>
inline void
reverse_array(Array<T, S>* arr)
{
  ASSERT(arr->memory != nullptr);
  for (size_t i = 0; i < arr->size / 2; i++)
  {
    size_t swapped_index = arr->size - i - 1;

    T temp = *array_at(arr, i);
    *array_at(arr, i) = *array_at(arr, swapped_index);
    *array_at(arr, swapped_index) = temp;
  }
}
