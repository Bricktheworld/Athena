#include "Core/Foundation/context.h"

#include "Core/Tools/AssetBuilder/texture_importer.h"

#include "Core/Tools/AssetBuilder/Vendor/StbImage/stb_image.h"

static constexpr u32 kTextureAssetVersion = 1;

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

  u32 uncompressed_size       = channels * width * height;

  out_imported_texture->hash              = asset_id;
  memcpy(out_imported_texture->path, path, strlen(path) + 1);

  out_imported_texture->width             = width;
  out_imported_texture->height            = height;
  out_imported_texture->color_space       = ColorSpaceName::kRec709;
  out_imported_texture->format            = TextureFormat::kRGBA8Unorm;
  out_imported_texture->uncompressed_size = uncompressed_size;
  out_imported_texture->compressed_size   = uncompressed_size;

  // TODO(bshihabi): Add texture compression (GDeflate?)
  out_imported_texture->compressed_buf   = HEAP_ALLOC(u8, heap, uncompressed_size);
  memcpy(out_imported_texture->compressed_buf, buf, uncompressed_size);

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
  dbgln("  Uncompressed Size: %lu", texture.uncompressed_size);
  dbgln("  Compressed Size: %lu", texture.compressed_size);
}

bool
asset_builder::write_texture_to_asset(
  const char* project_root,
  const ImportedTexture& texture
) {
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  u32 output_size = sizeof(TextureAsset) + texture.compressed_size;

  u8* buffer = HEAP_ALLOC(u8, scratch_arena, output_size);
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
  texture_asset.compressed_size          = texture.compressed_size;
  texture_asset.uncompressed_size        = texture.uncompressed_size;
  texture_asset.data                     = sizeof(TextureAsset);

  memcpy(buffer + offset, &texture_asset, sizeof(TextureAsset)); offset += sizeof(TextureAsset);
  memcpy(buffer + offset, texture.compressed_buf, texture.compressed_size); offset += texture.compressed_size;

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