#pragma once
#include "Core/Foundation/assets.h"

static constexpr u32 kPacketMagicNumber = CRC32_STR("ATHENA_PACKET");

enum PacketType : u32
{
  kPacketTypeUnknown = 0x0,
  kIAmGameRegister = CRC32_STR("I_AM_GAME"),
  kHotReloadShaderRequest = CRC32_STR("HOT_RELOAD_SHADER_REQUEST"),
};

struct PacketMetadata
{
  u32        magic_number;
  u32        version;
  PacketType type;
  u32        length;
};

struct PacketHotReloadShader
{
  PacketMetadata  metadata;

  OffsetPtr<char> name;
  OffsetPtr<u8>   shader_bin;
  u32             name_size;
  u32             shader_bin_size;
};
