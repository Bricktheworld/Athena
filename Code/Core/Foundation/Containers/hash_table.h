#pragma once
#include "Core/Foundation/memory.h"

#include "Core/Foundation/Containers/option.h"
#include "Core/Foundation/Containers/iterator.h"

#include "Core/Foundation/Vendor/xxhash/xxhash.h"

inline u64
hash_u64(const void* data, size_t size)
{
  return XXH64(data, size, 0);
}

enum HashTableCtrl : u8
{
  kHashTableCtrlEmpty    = 0x80,
  kHashTableCtrlDeleted  = 0xFF,
  kHashTableCtrlFullMask = 0x7F,
};

template <typename T>
concept Hashable = __has_unique_object_representations(T);

// Swiss-table implementation. Really simple to implement and really well optimized.
template <Hashable K, typename V>
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

  struct MutableKeyValue
  {
    K& key;
    V& value;
  };

  struct ConstKeyValue
  {
    const K& key;
    const V& value;
  };

  typedef u64 (*HashFunc)(const void* data, size_t size);

  Group* groups = nullptr;
  V* values = nullptr;
  HashFunc hash_func = &hash_u64;

  u64 groups_size = 0;
  u64 capacity = 0;

  u64 used = 0;

  Iterator<HashTable, MutableKeyValue> begin() { return Iterator<HashTable, MutableKeyValue>::begin(this);  }
  Iterator<HashTable, MutableKeyValue> end() { return Iterator<HashTable, MutableKeyValue>::end(this);  }
  Iterator<const HashTable, ConstKeyValue> begin() const { return Iterator<const HashTable, ConstKeyValue>::begin(this);  }
  Iterator<const HashTable, ConstKeyValue> end() const { return Iterator<const HashTable, ConstKeyValue>::end(this);  }
private:
  friend Iterator<HashTable, MutableKeyValue>;
  friend Iterator<const HashTable, ConstKeyValue>;

  MutableKeyValue operator[](size_t idx)
  {
    return {groups[idx / 16].keys[idx % 16], values[idx]};
  }


  const ConstKeyValue operator[](size_t idx) const
  {
    return {groups[idx / 16].keys[idx % 16], values[idx]};
  }

  size_t m_increment_idx(size_t idx) const
  { 
    ASSERT(idx < groups_size * 16);
    idx++;

    return m_increment_to_valid_idx(idx);
  }

  size_t m_increment_to_valid_idx(size_t idx) const
  {
    u64 start_index = idx / 16;
    u64 group_index = start_index;
    ASSERT(group_index < groups_size);

    u8  offset      = idx % 16;

    do
    {
      auto* group = groups + group_index;
      // We AND with the ctrl_empty mask because we also want to include
      // tombstones here.
      u16 mask = (u16)_mm_movemask_epi8(_mm_and_si128(_mm_set1_epi8(kHashTableCtrlEmpty), group->ctrls_sse));

      // Find the first non-empty/deleted element in the hashmap
      for (u8 i = offset; i < 16; i++)
      {
        if ((mask & (1 << i)) == 0)
        {
          return idx;
        }
 
        idx++;
      }

      offset      = 0;
      group_index = (group_index + 1) % groups_size;
    } while (group_index != start_index);

    return m_end_idx();
  }

  size_t m_decrement_idx(size_t idx) const
  { 
    ASSERT(idx > 0);
    size_t orig_idx = idx;
    idx--;

    u64 start_index = idx / 16;
    u64 group_index = start_index;

    u8  offset      = idx % 16;
    do
    {
      auto* group = groups + group_index;
      // We AND with the ctrl_empty mask because we also want to include
      // tombstones here.
      u16 mask = _mm_movemask_epi8(_mm_and_si128(_mm_set1_epi8(kHashTableCtrlEmpty), group->ctrls_sse));

      // Find the first non-empty/deleted element in the hashmap
      for (s16 i = offset; i >= 0; i--)
      {
        if ((mask & (1 << i)) == 0)
        {
          return idx;
        }
 
        idx--;
      }

      if (group_index == 0)
      {
        return orig_idx;
      }

      offset      = 15;
      group_index = (group_index - 1) % groups_size;
    } while (group_index != start_index);

    return orig_idx;

  }

  size_t m_begin_idx() const 
  {
    return m_increment_to_valid_idx(0);
  }
  size_t m_end_idx() const { return groups_size * 16; }
};

template <typename K, typename V>
inline HashTable<K, V>
init_hash_table(AllocHeap heap, u64 capacity)
{
  HashTable<K, V> ret = {};

  capacity = capacity * 4 / 3 + 15;
  ret.groups_size = capacity / 16;
  using GroupType = typename HashTable<K, V>::Group;
  ret.groups      = HEAP_ALLOC(GroupType, heap, ret.groups_size);
  zero_memory(ret.groups, ret.groups_size * sizeof(typename HashTable<K, V>::Group));
  ret.values      = HEAP_ALLOC(V, heap, capacity);
  zero_memory(ret.values, capacity * sizeof(V));
  ret.capacity    = capacity;
  ret.used        = 0;

  for (u64 i = 0; i < ret.groups_size; i++)
  {
    ret.groups[i].ctrls_sse = _mm_set1_epi8(kHashTableCtrlEmpty);
  }

  return ret;
}

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
      u16 mask = (u16)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(h.meta), group->ctrls_sse));
      if (mask != 0)
      {
        for (u8 i = 0; i < 16; i++)
        {
          if ((mask & (1 << i)) == 0)
            continue;
          if (group->keys[i] != key) [[unlikely]]
            continue;

          return &table->values[group_index * 16 + i];
        }
      }
    }
    {
      // We AND with the ctrl_empty mask because we also want to include
      // tombstones here.
      u16 mask = (u16)_mm_movemask_epi8(_mm_and_si128(_mm_set1_epi8(kHashTableCtrlEmpty), group->ctrls_sse));

      // Find the first empty/deleted element in the hashmap
      for (u8 i = 0; i < 16; i++)
      {
        if ((mask & (1 << i)) == 0)
          continue;
  
        group->ctrls[i] = (u8)h.meta & kHashTableCtrlFullMask;
        group->keys[i] = key;

        table->used++;
  
        V* ret = &table->values[group_index * 16 + i];
        zero_memory(ret, sizeof(V));
        return ret;
      }
    }

    group_index = (group_index + 1) % table->groups_size;
  } while (group_index != start_index);

  UNREACHABLE;
}

template <typename K, typename V>
inline V*
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
    u16 mask = (u16)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(h.meta), group->ctrls_sse));
    for (u8 i = 0; i < 16; i++)
    {
      if ((mask & (1 << i)) == 0)
        continue;
      if (group->keys[i] != key) [[unlikely]]
        continue;
      return &table->values[group_index * 16 + i];
    }

    u16 empty_mask = (u16)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(kHashTableCtrlEmpty), group->ctrls_sse));

    // If there is at least one non-empty element, then that means that the hash _had_
    // a place to go, but there obviously isn't one.
    if (empty_mask != 0)
      return nullptr;

    group_index = (group_index + 1) % table->groups_size;
  } while (group_index != start_index);

  return nullptr;
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
      if (group->keys[i] != key) [[unlikely]]
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

