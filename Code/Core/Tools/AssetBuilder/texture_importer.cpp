#include "Core/Tools/AssetBuilder/texture_importer.h"

#include "Core/Tools/AssetBuilder/Vendor/StbImage/stb_image.h"

#include "Core/Vendor/D3D12/d3d12.h"
#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include "Core/Vendor/D3D12/d3d12.h"
#pragma comment(lib, "d3d12.lib")

bool
asset_builder::import_texture(
  AllocHeap heap,
  const char* path,
  const char* project_root,
  ImportedTexture* out_imported_texture
) {
  if (path[0] == 0)
  {
    return false;
  }
  AssetId asset_id = path_to_asset_id(path);

  char full_path[kMaxPathLength];
  snprintf(full_path, kMaxPathLength, "%s/%s", project_root, path);

  s32 width, height, channels;
  stbi_set_flip_vertically_on_load(true);

  u8* buf = stbi_load(full_path, &width, &height, &channels, STBI_rgb_alpha);

  if (buf == nullptr)
  {
    printf("Failed to import texture %s through STBI!\n", full_path);
    return false;
  }

  u32 size = 4 * width * height;

  out_imported_texture->hash              = asset_id;
  memcpy(out_imported_texture->path, path, strlen(path) + 1);

  out_imported_texture->width       = width;
  out_imported_texture->height      = height;
  out_imported_texture->color_space = ColorSpaceName::kRec709;
  out_imported_texture->format      = TextureFormat::kRGBA8Unorm;
  out_imported_texture->buf         = HEAP_ALLOC(u8, heap, size);

  memcpy(out_imported_texture->buf, buf, size);

  stbi_image_free(buf);

  return true;
}

void
asset_builder::dump_imported_texture(ImportedTexture texture)
{
  ASSERT_MSG_FATAL(path_to_asset_id(texture.path) == texture.hash, "Imported texture path and hash do not match!");

  dbgln("Texture(0x%x): %s", texture.hash, texture.path);
  dbgln("  Width: %lu", texture.width);
  dbgln("  Height: %lu", texture.height);
  dbgln("  Color Space: %s", texture.color_space == ColorSpaceName::kRec709 ? "Rec709" : "Rec2020");
  dbgln("  Format: %s", texture_format_to_str(texture.format));
}

bool
asset_builder::write_texture_to_asset(
  ID3D12Device* device,
  const char* project_root,
  const ImportedTexture& texture
) {
  D3D12_RESOURCE_DESC desc = {0};
  desc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Alignment           = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.DepthOrArraySize    = 1;
  desc.Width               = texture.width;
  desc.Height              = texture.height;
  desc.MipLevels           = 1;
  desc.Format              = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count    = 1;
  desc.SampleDesc.Quality  = 0;
  desc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags               = D3D12_RESOURCE_FLAG_NONE;

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  u32 row_count;
  u64 row_byte_count;
  u64 total_size;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &row_count, &row_byte_count, &total_size);

  u32 output_size = (u32)(sizeof(TextureAsset) + total_size);

  u8* buffer = HEAP_ALLOC(u8, GLOBAL_HEAP, output_size);
  defer { HEAP_FREE(GLOBAL_HEAP, buffer); };
  u32 offset = 0;

  TextureAsset texture_asset = {0};
  texture_asset.metadata.magic_number    = kAssetMagicNumber;
  texture_asset.metadata.version         = kTextureAssetVersion;
  texture_asset.metadata.asset_type      = AssetType::kTexture;
  texture_asset.metadata.asset_hash      = texture.hash;
  texture_asset.format                   = texture.format;
  texture_asset.color_space              = texture.color_space;
  texture_asset.width                    = texture.width;
  texture_asset.height                   = texture.height;
  texture_asset.compressed_size          = (u32)total_size;
  texture_asset.uncompressed_size        = (u32)total_size;
  texture_asset.data                     = sizeof(TextureAsset);

  memcpy(buffer + offset, &texture_asset, sizeof(TextureAsset)); offset += sizeof(TextureAsset);
  {
    u32 src_row_pitch_bytes   = (4 * texture.width);
    // u32 src_slice_pitch_bytes = texture.height * src_row_pitch_bytes;

    offset += (u32)footprint.Offset;
    u32 dst_row_pitch_padded_bytes = (u32)footprint.Footprint.RowPitch;
    u32 dst_row_pitch_packed_bytes = (u32)row_byte_count;
    // u32 dst_slice_pitch_bytes      = (u32)row_count * dst_row_pitch_padded_bytes;

    u32 min_height                 = MIN(texture.height, (u32)row_count);
    u32 min_packed_row_pitch       = MIN(MIN(dst_row_pitch_padded_bytes, src_row_pitch_bytes), dst_row_pitch_packed_bytes);
    ASSERT_MSG_FATAL(min_packed_row_pitch == texture.width * 4, "Unsupported row pitch configuration!");
    for (u64 y = 0; y < min_height; y++)
    {
      // If you want to debug with just UVs
#if 0
      for (u64 x = 0; x < texture.width; x++)
      {
        Vec2 uv = Vec2((f32)x / (f32)texture.width, (f32)y / (f32)texture.height);
        u8 r = 255 *  uv.x;
        u8 g = 255 *  uv.y;
        u8 b = 0;
        u8 a = 255;
        buffer[offset + x * 4 + 0] = r;
        buffer[offset + x * 4 + 1] = g;
        buffer[offset + x * 4 + 2] = b;
        buffer[offset + x * 4 + 3] = a;
      }
#else
      memcpy(buffer + offset, texture.buf + y * texture.width * 4, min_packed_row_pitch);
#endif
      offset += dst_row_pitch_padded_bytes;
    }
  }

  char built_path[kMaxPathLength]{0};
  snprintf(built_path, sizeof(built_path), "%s/Assets/Built/0x%08x.built", project_root, texture.hash);
  printf("Writing texture asset file %s to %s...\n", texture.path, built_path);

  auto new_file = create_file(built_path, FileCreateFlags::kCreateTruncateExisting);
  if (!new_file)
  {
    printf("Failed to create output file!\n");
    return false;
  }

  defer { close_file(&new_file.value()); };

  if (!write_file(new_file.value(), buffer, output_size))
  {
    printf("Failed to write output file!\n");
    return false;
  }

  return true;
}