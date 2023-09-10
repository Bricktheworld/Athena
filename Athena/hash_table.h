#pragma once
#include "memory/memory.h"
#include "option.h"
#include "vendor/xxhash/xxhash.h"

inline u64
hash_u64(const void* data, size_t size)
{
  return XXH64(data, size, 0);
}

enum HashTableCtrl : u8
{
  kHashTableCtrlEmpty     = 0x80,
  kHashTableCtrlDeleted   = 0xFF,
  kHashTableCtrlFullMask = 0x7F,
};

// Swiss-table implementation. Really simple to implement and really well optimized.
template <typename K, typename V>
struct HashTable
{
  union Hash
  {
    struct
    {
      // 7 bits of metadata
      u64 meta : 7;
      // The remaining bits are for the position index.
      u64 position : 57;
      // 57 + 7 = 64 bits
    };
    u64 raw = 0;
  };

  struct Group
  {
    union
    {
      u8x16 ctrls_sse;
      u8 ctrls[16];
    };
    K keys[16];
  };

  typedef u64 (*HashFunc)(const void* data, size_t size);

  Group* groups = nullptr;
  V* values = nullptr;
  HashFunc hash_func = &hash_u64;

  u64 groups_size = 0;
  u64 capacity = 0;

  u64 used = 0;

#if 0
  Iterator<HashTable, T> begin() { return Iterator<Container, T>::begin(this);  }
  Iterator<Container, T> end() { return Iterator<Container, T>::end(this);  }
  Iterator<const Container, const T> begin() const { return Iterator<const Container, const T>::begin(this);  }
  Iterator<const Container, const T> end() const { return Iterator<const Container, const T>::end(this);  }
private:
  friend Iterator<Container, T>;
  friend Iterator<const Container, const T>;
  size_t m_increment_idx(size_t idx) const
  { 
    ASSERT(idx < groups_size * 16);
    idx++;

    while (idx / 16 < groups_size)
    {
      HashTable::Group* group = this->groups + idx / 16;
      u16 mask = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(kHashTableCtrlFullMask), group->ctrls_sse));

      do
      {
        u64 shift = idx % 16;
        if ((mask & (1 << shift)) != 0)
          return idx;

        idx++;
      }
      while (idx % 16 != 0);
    }

    ASSERT(idx == groups_size * 16);
    return idx;
  }

  size_t m_decrement_idx(size_t idx) const
  { 
    ASSERT(idx > 0);
    idx--;

    while (idx > 0)
    {
      HashTable::Group* group = this->groups + idx / 16;
      u16 mask = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(kHashTableCtrlFullMask), group->ctrls_sse));

      do
      {
        u64 shift = idx % 16;
        if ((mask & (1 << shift)) != 0)
          return idx;

        idx--;
      }
      while (idx % 16 != 0);
    }

    ASSERT(idx == 0);
    return idx;
  }

  size_t m_begin_idx() const { return m_increment_idx(0); }
  size_t m_end_idx() const { return groups_size * 16; }
#endif
};

template <typename K, typename V>
inline HashTable<K, V>
init_hash_table(MEMORY_ARENA_PARAM, u64 capacity)
{
  HashTable<K, V> ret = {};

  capacity = capacity * 4 / 3 + 15;
  ret.groups_size = capacity / 16;
  ret.groups      = push_memory_arena<typename HashTable<K, V>::Group>(MEMORY_ARENA_FWD, ret.groups_size);
  zero_memory(ret.groups, ret.groups_size * sizeof(typename HashTable<K, V>::Group));
  ret.values      = push_memory_arena<V>(MEMORY_ARENA_FWD, capacity);
  ret.capacity    = capacity;
  ret.used        = 0;

  for (u64 i = 0; i < ret.groups_size; i++)
  {
    ret.groups[i].ctrls_sse = _mm_set1_epi8(kHashTableCtrlEmpty);
  }

  return ret;
}

// TODO(Brandon): Add branch prediction hints

template <typename K, typename V>
inline V*
hash_table_insert(HashTable<K, V>* table, const K& key)
{
  ASSERT(table->used < table->capacity);

  using Hash = typename HashTable<K, V>::Hash;

  Hash h = {0};
  h.raw = table->hash_func(&key, sizeof(key));

  u64 start_index = h.position % table->groups_size;
  u64 group_index = start_index;
  do
  {
    auto* group = table->groups + group_index;
    {
      // Look for already existing key.
      u16 mask = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(h.meta), group->ctrls_sse));
      if (mask != 0)
      {
        for (u8 i = 0; i < 16; i++)
        {
          if ((mask & (1 << i)) == 0)
            continue;
          if (group->keys[i] != key)
            continue;

          return &table->values[group_index * 16 + i];
        }
      }
    }
    {
      // We AND with the ctrl_empty mask because we also want to include
      // tombstones here.
      u16 mask = _mm_movemask_epi8(_mm_and_si128(_mm_set1_epi8(kHashTableCtrlEmpty), group->ctrls_sse));

      // Find the first empty/deleted element in the hashmap
      for (u8 i = 0; i < 16; i++)
      {
        if ((mask & (1 << i)) == 0)
          continue;
  
        group->ctrls[i] = (u8)h.meta & kHashTableCtrlFullMask;
        group->keys[i] = key;

        table->used++;
  
        return &table->values[group_index * 16 + i];
      }
    }

    group_index = (group_index + 1) % table->groups_size;
  } while (group_index != start_index);

  UNREACHABLE;
  return nullptr;
}

template <typename K, typename V>
inline Option<V*>
hash_table_find(const HashTable<K, V>* table, const K& key)
{
  using Hash = typename HashTable<K, V>::Hash;

  Hash h = {0};
  h.raw = table->hash_func(&key, sizeof(key));

  u64 start_index = h.position % table->groups_size;
  u64 group_index = start_index;
  do
  {
    auto* group = table->groups + group_index;
    u16 mask = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(h.meta), group->ctrls_sse));
    for (u8 i = 0; i < 16; i++)
    {
      if ((mask & (1 << i)) == 0)
        continue;
      if (group->keys[i] != key)
        continue;
      return &table->values[group_index * 16 + i];
    }

    u16 empty_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(kHashTableCtrlEmpty), group->ctrls_sse));

    // If there is at least one non-empty element, then that means that the hash _had_
    // a place to go, but there obviously isn't one.
    if (empty_mask != 0)
      return None;

    group_index = (group_index + 1) % table->groups_size;
  } while (group_index != start_index);

  return None;
}

template <typename K, typename V>
inline bool
hash_table_erase(HashTable<K, V>* table, const K& key)
{
  using Hash = typename HashTable<K, V>::Hash;

  Hash h = {0};
  h.raw = table->hash_func(&key, sizeof(key));

  u64 start_index = h.position % table->groups_size;
  u64 group_index = start_index;
  do
  {
    auto* group = table->groups + group_index;
    u16 mask = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(h.meta), group->ctrls_sse));
    u16 empty_mask = _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(kHashTableCtrlEmpty), group->ctrls_sse));
    for (u8 i = 0; i < 16; i++)
    {
      if ((mask & (1 << i)) == 0)
        continue;
      if (group->keys[i] != key)
        continue;

      table->used--;
      group->ctrls[i] = empty_mask == 0 ? kHashTableCtrlDeleted : kHashTableCtrlEmpty;
      return true;
    }

    // If there is at least one non-empty element, then that means that the hash _had_
    // a place to go, but there obviously isn't one.
    if (empty_mask != 0)
      return false;

    group_index = (group_index + 1) % table->groups_size;
  } while (group_index != start_index);

  return false;
}

