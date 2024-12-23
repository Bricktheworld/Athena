#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/math.h"
#include "Core/Foundation/memory.h"
#include "Core/Foundation/filesystem.h"
#include "Core/Foundation/assets.h"
#include "Core/Foundation/colors.h"

namespace asset_builder
{      
  struct ImportedTexture
  {
    AssetId        hash;
    char           path[kMaxPathLength];

    u32            width;
    u32            height;
    u32            compressed_size;
    u32            uncompressed_size;
    ColorSpaceName color_space;
    TextureFormat  format;

    u8*            compressed_buf;
  };
  
  
  check_return bool import_texture(
    AllocHeap heap,
    const char* path,
    const char* project_root,
    ImportedTexture* out_imported_texture
  );

  void dump_imported_texture(ImportedTexture texture);

  check_return bool write_texture_to_asset(
    const char* project_root,
    const ImportedTexture& texture
  );
}