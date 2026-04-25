#include "Core/Foundation/assets.h"

void asset_id_to_path(char* dst, AssetId asset_id)
{
  static const char kLut[] =
    "000102030405060708090a0b0c0d0e0f"
    "101112131415161718191a1b1c1d1e1f"
    "202122232425262728292a2b2c2d2e2f"
    "303132333435363738393a3b3c3d3e3f"
    "404142434445464748494a4b4c4d4e4f"
    "505152535455565758595a5b5c5d5e5f"
    "606162636465666768696a6b6c6d6e6f"
    "707172737475767778797a7b7c7d7e7f"
    "808182838485868788898a8b8c8d8e8f"
    "909192939495969798999a9b9c9d9e9f"
    "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
    "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
    "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
    "d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
    "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
    "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";

  memcpy(dst, kAssetPathFmt, sizeof(kAssetPathFmt));
  dst += sizeof("Assets/Built/0xXXXXXXXX") - 2;

  for (u8 i = 0; i < 4; i++)
  {
    u32         pos = (asset_id & 0xFF) * 2 + 1;
    const char* ch  = kLut + pos;

    *dst-- = *ch--;
    *dst-- = *ch--;

    asset_id >>= 8;
  }
}

void asset_id_to_path_w(wchar_t* dst, AssetId asset_id)
{
  static const wchar_t kLut[] =
    L"000102030405060708090a0b0c0d0e0f"
    L"101112131415161718191a1b1c1d1e1f"
    L"202122232425262728292a2b2c2d2e2f"
    L"303132333435363738393a3b3c3d3e3f"
    L"404142434445464748494a4b4c4d4e4f"
    L"505152535455565758595a5b5c5d5e5f"
    L"606162636465666768696a6b6c6d6e6f"
    L"707172737475767778797a7b7c7d7e7f"
    L"808182838485868788898a8b8c8d8e8f"
    L"909192939495969798999a9b9c9d9e9f"
    L"a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
    L"b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
    L"c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
    L"d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
    L"e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
    L"f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";

  memcpy(dst, kAssetPathFmtW, sizeof(kAssetPathFmtW));
  dst += sizeof("Assets/Built/0xXXXXXXXX") - 2;

  for (u8 i = 0; i < 4; i++)
  {
    u32            pos = (asset_id & 0xFF) * 2 + 1;
    const wchar_t* ch  = kLut + pos;

    *dst-- = *ch--;
    *dst-- = *ch--;

    asset_id >>= 8;
  }
}

Result<FileStream, FileError>
open_built_asset_file(AssetId asset)
{
  char path[512];
  snprintf(path, 512, "Assets/Built/0x%08x.built", asset);
  return open_file(path, kFileStreamRead);
}

AssetType
get_asset_type(const void* buffer, size_t size)
{
  if (size < sizeof(AssetMetadata))
  {
    return AssetType::kUnknown;
  }
  AssetMetadata* metadata = (AssetMetadata*)buffer;

  if (metadata->magic_number != kAssetMagicNumber)
  {
    return AssetType::kUnknown;
  }

  return metadata->asset_type;
}

