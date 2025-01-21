#include "Core/Foundation/tlsf_allocator.h"

UFloat8
u32_to_ufloat8_round_up(u32 size)
{
  // Denorm
  if (size < UFloat8::kMantissaValue)
  {
    UFloat8 ret = {0};
    ret.as_float.exponent = 0;
    ret.as_float.mantissa = size;
    return ret;
  }

  u32 highest_set_bit = 31 - count_leading_zeroes(size);

  u32 mantissa_start_bit = highest_set_bit - UFloat8::kMantissaBits;

  u32 exponent  = mantissa_start_bit + 1;
  u32 mantissa  = (size >> mantissa_start_bit) & UFloat8::kMantissaMask;

  u32 precision_err_mask = (1 << mantissa_start_bit) - 1;

  if ((size & precision_err_mask) != 0)
  {
    mantissa++;
  }

  UFloat8 ret = {0};
  ret.as_uint = (exponent << UFloat8::kMantissaBits) + mantissa;
  return ret;
}


UFloat8
u32_to_ufloat8_round_down(u32 size)
{
  // Denorm
  if (size < UFloat8::kMantissaValue)
  {
    UFloat8 ret = {0};
    ret.as_float.exponent = 0;
    ret.as_float.mantissa = size;
    return ret;
  }

  u32 highest_set_bit = 31 - count_leading_zeroes(size);

  u32 mantissa_start_bit = highest_set_bit - UFloat8::kMantissaBits;

  u32 exponent  = mantissa_start_bit + 1;
  u32 mantissa  = (size >> mantissa_start_bit) & UFloat8::kMantissaMask;

  UFloat8 ret = {0};
  ret.as_uint = (exponent << UFloat8::kMantissaBits) | mantissa;
  return ret;
}

u32
ufloat8_to_u32(UFloat8 val)
{
  // Denorm
  if (val.as_float.exponent == 0)
  {
    return val.as_float.mantissa;
  }

  return (val.as_float.mantissa | UFloat8::kMantissaValue) << (val.as_float.exponent - 1);
}

static constexpr u32 kNumTopBins     = 1ULL << UFloat8::kExponentBits;
static constexpr u32 kNumBinsPerLeaf = 1ULL << UFloat8::kMantissaBits;
static constexpr u32 kNumLeafBins    = kNumTopBins * kNumBinsPerLeaf;

static u32
insert_node_into_bin(TlsfSubAllocator* self, u32 size, u32 offset)
{
  UFloat8 bin_index = u32_to_ufloat8_round_down(size);

  u32 top_bin_index  = bin_index.as_float.exponent;
  u32 leaf_bin_index = bin_index.as_float.mantissa;

  if (self->bin_indices[bin_index.as_uint] == TlsfSubAllocator::kNullIndex)
  {
    self->used_top                 |= 1ULL << top_bin_index;
    self->used_leaf[top_bin_index] |= 1ULL << leaf_bin_index;
  }

  u32 next_node = self->bin_indices[bin_index.as_uint];
  u32 new_idx   = self->free_stack[self->free_offset--];

  auto* dst = self->nodes + new_idx;
  dst->offset   = offset;
  dst->size     = size;
  dst->bin_next = next_node;

  if (next_node != TlsfSubAllocator::kNullIndex)
  {
    self->nodes[next_node].bin_prev = new_idx;
  }

  self->bin_indices[bin_index.as_uint] = new_idx;

  return new_idx;
}

TlsfSubAllocator
init_tlsf_sub_allocator(AllocHeap heap, u32 size, u32 max_allocs)
{
  TlsfSubAllocator ret = {0};
  ret.size       = size;
  ret.max_allocs = max_allocs;

  for (u32 i = 0; i < ARRAY_LENGTH(ret.bin_indices); i++)
  {
    ret.bin_indices[i] = TlsfSubAllocator::kNullIndex;
  }

  ret.nodes      = HEAP_ALLOC(TlsfSubAllocator::BinNode, heap, ret.max_allocs);
  ret.free_stack = HEAP_ALLOC(u32, heap, ret.max_allocs);

  zero_memory(ret.nodes, sizeof(TlsfSubAllocator::BinNode) * ret.max_allocs);
  for (u32 i = 0; i < ret.max_allocs; i++)
  {
    ret.free_stack[i] = ret.max_allocs - i - 1;
  }

  insert_node_into_bin(&ret, size, 0);
  return ret;
}

SubAllocation
tlsf_sub_alloc(void* tlsf, u32 size, u32 alignment)
{
  UNREFERENCED_PARAMETER(tlsf);
  UNREFERENCED_PARAMETER(size);
  UNREFERENCED_PARAMETER(alignment);
  SubAllocation ret;
  return ret;
}

void
tlsf_sub_free(void* tlsf, SubAllocation allocation)
{
  UNREFERENCED_PARAMETER(tlsf);
  UNREFERENCED_PARAMETER(allocation);
}
