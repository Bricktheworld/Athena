#include "Core/Foundation/Gpu/gpu.h"

#include "Core/Tools/AssetBuilder/texture_importer.h"

#include "Core/Tools/AssetBuilder/Vendor/StbImage/stb_image.h"
#include "Core/Tools/AssetBuilder/Vendor/DirectXTex/DirectXTex.h"
#include "Core/Tools/AssetBuilder/Vendor/bc7enc/bc7enc.h"

#include "Core/Vendor/D3D12/d3d12.h"
#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include "Core/Vendor/D3D12/d3d12.h"
#pragma comment(lib, "d3d12.lib")

bool
import_texture(
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

  const char* extension = path + get_file_extension(path, (u32)strlen(path));

  // Use DirecXTex for certain file types
  if (_stricmp(extension, ".dds") == 0)
  {
    wchar_t wpath[kMaxPathLength];
    mbstowcs_s(nullptr, wpath, path, 1024);

    DirectX::ScratchImage scratch_img;
    DirectX::TexMetadata metadata;
    HRESULT hres = DirectX::LoadFromDDSFile(wpath, DirectX::DDS_FLAGS_NONE, &metadata, scratch_img);
    if (FAILED(hres))
    {
      printf("Failed to import DDS texture %s through DirectXTex!\n", full_path);
      HASSERT(hres);
      return false;
    }

    if (scratch_img.GetImageCount() == 0)
    {
      printf("Unsupported image count %llu\n", scratch_img.GetImageCount());
      return false;
    }

    const DirectX::Image* img = scratch_img.GetImage(0, 0, 0);

    if (DirectX::IsCompressed(img->format))
    {
      DirectX::ScratchImage uncompressed_img;
      hres = DirectX::Decompress(*img, DXGI_FORMAT_R8G8B8A8_UNORM, uncompressed_img);
      if (FAILED(hres))
      {
        printf("Failed to uncompress texture with DXGI format %u\n", img->format);
        HASSERT(hres);
        return false;
      }

      scratch_img = std::move(uncompressed_img);
      img         = scratch_img.GetImage(0, 0, 0);
    }

    if (img->format != DXGI_FORMAT_R8G8B8A8_UNORM)
    {
      DirectX::ScratchImage formatted_img;
      hres = DirectX::Convert(*img, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, formatted_img);

      if (FAILED(hres))
      {
        printf("Failed to convert DXGI format %u to DXGI_FORMAT_R8G8B8A8_UNORM\n", img->format);
        HASSERT(hres);
        return false;
      }
      scratch_img = std::move(formatted_img);
      img         = scratch_img.GetImage(0, 0, 0);
    }

    if (img->format != DXGI_FORMAT_R8G8B8A8_UNORM)
    {
      printf("Unsupported image format %u!\n", img->format);
      return false;
    }

    u32 width  = (u32)img->width;
    u32 height = (u32)img->height;

    u32 size   = 4 * width * height;

    out_imported_texture->hash        = asset_id;
    memcpy(out_imported_texture->path, path, strlen(path) + 1);
    out_imported_texture->width       = width;
    out_imported_texture->height      = height;
    out_imported_texture->color_space = ColorSpaceName::kRec709;
    out_imported_texture->format      = TextureFormat::kRGBA8Unorm;
    out_imported_texture->buf         = HEAP_ALLOC(u8, heap, size);

    const u32 row_pitch = (u32)img->rowPitch;
    for (u32 y = 0; y < height; y++)
    {
      for (u32 x = 0; x < width; x++)
      {
        const u8* src = img->pixels + y * row_pitch + x * 4;
              u8* dst = out_imported_texture->buf + (y * width + x) * 4;
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
      }
    }

    return true;
  }
  // Use stbimage for others
  else
  {
    s32 width    = 0;
    s32 height   = 0;
    s32 channels = 0;
    stbi_set_flip_vertically_on_load(true);

    u8* buf      = stbi_load(full_path, &width, &height, &channels, STBI_rgb_alpha);
    if (buf == nullptr)
    {
      printf("Failed to import texture %s through STBI!\n", full_path);
      return false;
    }

    u32 size = 4 * width * height;

    out_imported_texture->hash        = asset_id;
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
}

void
dump_imported_texture(ImportedTexture texture)
{
  ASSERT_MSG_FATAL(path_to_asset_id(texture.path) == texture.hash, "Imported texture path and hash do not match!");

  dbgln("Texture(0x%x): %s", texture.hash, texture.path);
  dbgln("  Width: %lu", texture.width);
  dbgln("  Height: %lu", texture.height);
  dbgln("  Color Space: %s", texture.color_space == ColorSpaceName::kRec709 ? "Rec709" : "Rec2020");
  dbgln("  Format: %s", texture_format_to_str(texture.format));
}

struct BCCompressionStats
{
  u32 blocks_x          = 0;
  u32 blocks_y          = 0;
  u32 uncompressed_size = 0;
  u32 compressed_size   = 0;
};

static BCCompressionStats
get_bc7_compression_stats(const ImportedTexture& texture)
{
  BCCompressionStats ret = {0};
  ret.blocks_x           = UCEIL_DIV(texture.width,  4);
  ret.blocks_y           = UCEIL_DIV(texture.height, 4);
  ret.uncompressed_size  = ret.blocks_x * ret.blocks_y * BC7ENC_BLOCK_SIZE;
  return ret;
}

struct GpuTextureCopyableFootprint
{
  u64 offset                = 0;
  u64 row_count             = 0;
  u64 row_byte_count        = 0;
  u64 row_padded_byte_count = 0;
  u64 total_size            = 0;
};

static u32
get_uncompressed_bytes_per_pixel(const ImportedTexture& src)
{
  switch (src.format)
  {
    case TextureFormat::kRGBA8Unorm:   return 4;
    case TextureFormat::kRGB10A2Unorm: return 4;
    case TextureFormat::kRGBA16Float:  return 8;
    default: UNREACHABLE;
  }
}

static DXGI_FORMAT gpu_format_to_d3d12(GpuFormat format)
{
  return (DXGI_FORMAT)format;
}

// TODO(bshihabi): How will this work with other graphics API backends in the future? I'm guessing
// we'll have to have a platform independent and platform dependent built asset files in the future.
static GpuTextureCopyableFootprint
get_d3d12_texture_copyable_footprint(ID3D12Device* device, const ImportedTexture& texture, GpuFormat dst_format)
{
  D3D12_RESOURCE_DESC desc = {0};
  desc.Dimension           = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Alignment           = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.DepthOrArraySize    = 1;
  desc.Width               = texture.width;
  desc.Height              = texture.height;
  desc.MipLevels           = 1;
  desc.Format              = gpu_format_to_d3d12(dst_format);
  desc.SampleDesc.Count    = 1;
  desc.SampleDesc.Quality  = 0;
  desc.Layout              = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags               = D3D12_RESOURCE_FLAG_NONE;

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  u32 row_count;
  u64 row_byte_count;
  u64 total_size;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &row_count, &row_byte_count, &total_size);

  GpuTextureCopyableFootprint ret;
  ret.offset                = footprint.Offset;
  ret.row_count             = row_count;
  ret.row_byte_count        = row_byte_count;
  ret.row_padded_byte_count = footprint.Footprint.RowPitch;
  ret.total_size            = total_size;
  return ret;
}

static u64
compress_bc7_write_to_buffer(u8* dst_base, const ImportedTexture& texture, const BCCompressionStats& stats, const GpuTextureCopyableFootprint& footprint)
{
  bc7enc_compress_block_init();

  bc7enc_compress_block_params params;
  bc7enc_compress_block_params_init(&params);

  u32 bpp = get_uncompressed_bytes_per_pixel(texture);
  u8* dst = dst_base + footprint.offset;

  for (u32 by = 0; by < stats.blocks_y; by++)
  {
    u8* row_dst = dst + by * footprint.row_padded_byte_count;

    for (u32 bx = 0; bx < stats.blocks_x; bx++)
    {
      color_rgba uncompressed_block[16];
      for (u32 py = 0; py < 4; py++)
      {
        for (u32 px = 0; px < 4; px++)
        {
                u32 src_x = MIN(bx * 4 + px, texture.width  - 1);
                u32 src_y = MIN(by * 4 + py, texture.height - 1);
          const u8* src   = texture.buf + (src_y * texture.width + src_x) * bpp;
          uncompressed_block[py * 4 + px] = { src[0], src[1], src[2], src[3] };
        }
      }

      bc7enc_compress_block(row_dst + bx * BC7ENC_BLOCK_SIZE, uncompressed_block, &params);
    }
  }

  return footprint.total_size;
}


static u64
uncompressed_write_to_buffer(u8* dst_base, const ImportedTexture& texture, const GpuTextureCopyableFootprint& footprint)
{
  u32 bpp                        = get_uncompressed_bytes_per_pixel(texture);
  u32 src_row_pitch_bytes        = bpp * texture.width;

  u8* dst = dst_base;

  dst += footprint.offset;
  u32 dst_row_pitch_padded_bytes = (u32)footprint.row_padded_byte_count;
  u32 dst_row_pitch_packed_bytes = (u32)footprint.row_byte_count;

  u64 min_height                 = MIN(texture.height, footprint.row_count);
  u64 min_packed_row_pitch       = MIN(MIN(dst_row_pitch_padded_bytes, src_row_pitch_bytes), dst_row_pitch_packed_bytes);
  ASSERT_MSG_FATAL(min_packed_row_pitch == texture.width * bpp, "Unsupported row pitch configuration!");
  for (u64 y = 0; y < min_height; y++)
  {
    u8* dst_row = ALLOC_OFF(dst, dst_row_pitch_padded_bytes);
    memcpy(dst_row, texture.buf + y * texture.width * 4, min_packed_row_pitch);
  }

  return dst - dst_base;
}

bool
write_texture_to_asset(
  ID3D12Device* device,
  const char* project_root,
  const ImportedTexture& texture
) {
  TextureCompression          compression = TextureCompression::kBc7; // TextureCompression::kUncompressed;
  GpuFormat                   format      = kGpuFormatBC7Unorm; // kGpuFormatRGBA8Unorm;
  BCCompressionStats          stats       = get_bc7_compression_stats(texture);
  GpuTextureCopyableFootprint footprint   = get_d3d12_texture_copyable_footprint(device, texture, format);

  u32 output_size = (u32)(sizeof(TextureAsset) + footprint.total_size);

  u8* buffer = HEAP_ALLOC(u8, GLOBAL_HEAP, output_size);
  defer { HEAP_FREE(GLOBAL_HEAP, buffer); };

  u8* dst    = buffer;

  TextureAsset texture_asset = {0};
  texture_asset.metadata.magic_number    = kAssetMagicNumber;
  texture_asset.metadata.version         = kTextureAssetVersion;
  texture_asset.metadata.asset_type      = AssetType::kTexture;
  texture_asset.metadata.asset_hash      = texture.hash;
  texture_asset.texture_compression      = compression;
  texture_asset.gpu_format               = format;
  texture_asset.color_space              = texture.color_space;
  texture_asset.width                    = texture.width;
  texture_asset.height                   = texture.height;
  // NOTE(bshihabi): I intentionally use the BC compressed size as the "uncompressed size". This is because
  // "uncompressed size" means what is consumed after LZ compression. It is used as a "check" of sorts in the runtime.
  // I never block decompress the raw texture so there is no point in storing that information in the texture anywhere.
  texture_asset.compressed_size          = (u32)footprint.total_size;
  texture_asset.uncompressed_size        = (u32)footprint.total_size;
  texture_asset.data                     = sizeof(TextureAsset);

  memcpy(dst, &texture_asset, sizeof(TextureAsset)); dst += sizeof(TextureAsset);
  dst += compress_bc7_write_to_buffer(dst, texture, stats, footprint);
  // ASSERT_MSG_FATAL(dst - buffer == sizeof(TextureAsset) + texture_asset.uncompressed_size, "Wrote out incorrect number of bytes to final texture! Expected 0x%llx but got 0x%llx", texture_asset.uncompressed_size, dst - buffer - sizeof(TextureAsset));

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