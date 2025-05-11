#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/math.h"
#include "Core/Foundation/memory.h"
#include "Core/Foundation/filesystem.h"
#include "Core/Foundation/assets.h"
#include "Core/Foundation/colors.h"

struct ID3D12Device;

namespace asset_builder
{      
  struct ImportedTexture
  {
    AssetId        hash;
    char           path[kMaxPathLength];

    u32            width;
    u32            height;
    ColorSpaceName color_space;
    TextureFormat  format;

    u8*            buf;
  };
  
  
  DONT_IGNORE_RETURN bool import_texture(
    AllocHeap heap,
    const char* path,
    const char* project_root,
    ImportedTexture* out_imported_texture
  );

  void dump_imported_texture(ImportedTexture texture);

  DONT_IGNORE_RETURN bool write_texture_to_asset(
    ID3D12Device* device,
    const char* project_root,
    const ImportedTexture& texture
  );
}