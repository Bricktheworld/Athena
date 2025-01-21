#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/memory.h"

// Software implementation of an unsigned 8 bit float
struct UFloat8
{
  static constexpr u32 kMantissaBits = 3;
  static constexpr u32 kExponentBits = 5;

  static constexpr u32 kMantissaValue = 1ULL << kMantissaBits;
  static constexpr u32 kMantissaMask  = kMantissaValue - 1;

  union
  {
    struct
    {
      u32 mantissa: kMantissaBits;
      u32 exponent: kExponentBits;
    } as_float;

    u32 as_uint;
  };
};
UFloat8 u32_to_ufloat8_round_up(u32 size);
UFloat8 u32_to_ufloat8_round_down(u32 size);
u32     ufloat8_to_u32(UFloat8 val);

struct TlsfSubAllocator
{
  static constexpr u32 kNumTopBins     = 1ULL << UFloat8::kExponentBits;
  static constexpr u32 kNumBinsPerLeaf = 1ULL << UFloat8::kMantissaBits;
  static constexpr u32 kNumLeafBins    = kNumTopBins * kNumBinsPerLeaf;
  static constexpr u32 kNullIndex      = 0xFFFFFFFF;

  struct BinNode
  {
    u32 offset         = 0;
    u32 size           = 0;

    u32 bin_prev       = 0;
    u32 bin_next       = 0;
    u32 neighbor_next  = 0;
    u32 neighbor_prev  = 0;
  };

  u32      size        = 0;

  u32      max_allocs  = 0;
  BinNode* nodes       = nullptr;
  u32*     free_stack  = nullptr;
  u32      free_offset = 0;

  // Each bit signifies whether the that top level bin is completely full
  u32      used_top    = 0;
  static_assert(kNumTopBins == 32, "The used_top bitfield no longer makes sense");
  // Each bit signifies whether the bottom level leaf bin is completely full
  u8       used_leaf[kNumTopBins];
  static_assert(kNumLeafBins == 8 * kNumTopBins, "The used_leaf bitfield no longer makes sense");

  u32      bin_indices[kNumLeafBins];
};

TlsfSubAllocator init_tlsf_sub_allocator(AllocHeap heap, u32 size, u32 max_allocs);

SubAllocation tlsf_sub_alloc(void* tlsf, u32 size, u32 alignment);
void          tlsf_sub_free (void* tlsf, SubAllocation allocation);
