#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/math.h"
#include "Core/Foundation/hash.h"
#include "Core/Foundation/filesystem.h"
#include "Core/Foundation/colors.h"

#include "Core/Foundation/Containers/array.h"

// NOTE(bshihabi): Keep in sync with interlop.hlsli!
struct VertexAsset
{
  Vec3 position; // Position MUST be at the START of the struct in order for BVHs to be built
  Vec3 normal;
  Vec2 uv;
};
static_assert(sizeof(VertexAsset) == sizeof(f32) * 8);

// Asset ID is a CRC32 hash of asset path...
typedef u32 AssetId;

static constexpr u32 kNullAssetId = 0x00000000;

static constexpr u32 kMaxPathLength = 512;

template <typename T>
using AssetRef = AssetId;

static constexpr u32 kAssetMagicNumber = CRC32_STR("ATHENA_ASSET");

static constexpr u32 kModelAssetVersion    = 2;
static constexpr u32 kTextureAssetVersion  = 2;
static constexpr u32 kMaterialAssetVersion = 2;

// The path is relative to the project root directory, so should be something like
// Assets/Source/model.fbx
// The asset ID is case insensitive
inline constexpr bool
is_slash(char c)
{
  return c == '/' || c == '\\';
}

inline constexpr char
char_to_lower(const char c)
{
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

inline constexpr AssetId
path_to_asset_id(const char* path)
{
  char buf[512];
  u32 len = 0;
  while (*path)
  {
    const char* cur = path;
          char  c   = *cur;
    path++;

    if (is_slash(c))
    {
      if (len == 0 || is_slash(*(cur - 1)))
      {
        continue;
      }

      c = '/';
    }

    buf[len++] = char_to_lower(c);
  }
  buf[len] = 0;

  return crc32(buf, len, 0);
}

#define ASSET_ID(A) std::integral_constant<AssetId, path_to_asset_id(A)>::value
FOUNDATION_API Result<FileStream, FileError> open_built_asset_file(AssetId asset);

enum struct TextureFormat : u32
{
  kRGBA8Unorm,
  kRGB10A2Unorm,
  kRGBA16Float,
};

inline const char*
texture_format_to_str(TextureFormat format)
{
  switch(format)
  {
    case TextureFormat::kRGBA8Unorm:   return "RGBA8Unorm";
    case TextureFormat::kRGB10A2Unorm: return "RGB10A2Unorm";
    case TextureFormat::kRGBA16Float:  return "RGBA16Float";
    default: UNREACHABLE;
  }
}

enum struct ShaderType : u32
{
  kVertex,
  kPixel,
  kCompute,
};

enum struct AssetType : u32
{
  kModel,
  kTexture,
  kShader,
  kMaterial,

  kCount,

  kUnknown,
};


struct ModelSubsetData
{
  Array<VertexAsset> vertices;
  Array<u32>         indices;
  AssetId            material;
};

struct ModelData
{
  Array<ModelSubsetData> model_subsets;
};

struct MaterialData
{
  AssetId        shader;
  Array<AssetId> textures;
};

enum struct AssetLoadResult : u32
{
  kOk,
  kErrCorrupted,
  kErrMismatchedAssetType,
};

FOUNDATION_API AssetType get_asset_type(const void* buffer, size_t size);

FOUNDATION_API check_return AssetLoadResult load_model(
  AllocHeap heap,
  const void* buffer,
  size_t size,
  ModelData* out_model
);

FOUNDATION_API void dump_model_info(const ModelData& model);

struct AssetMetadata
{
  u32       magic_number;
  u32       version;
  AssetType asset_type;
  u32       asset_hash;
};
ASSERT_SERIALIZABLE(AssetMetadata);

struct TextureAsset
{
  AssetMetadata  metadata;
  TextureFormat  format;
  ColorSpaceName color_space;
  u32            width;
  u32            height;
  u32            compressed_size;
  u32            uncompressed_size;
  OffsetPtr<u8>  data;
};
ASSERT_SERIALIZABLE(TextureAsset);

struct ShaderAsset
{
  AssetMetadata metadata;
  ShaderType    type;
  u32           __pad0__;
  u64           size;
  OffsetPtr<u8> data;
};
ASSERT_SERIALIZABLE(ShaderAsset);

// TODO(Brandon): Materials are always hard because how generic do we want to make it?
// For now I'm not gonna deal with complex overrides and will simply stick to that
// all materials are the same size and take in the same reasonable number of inputs...
// It'll probably all get set through the DCC anyway and anything more complex will probably have its own
// system that's not a part of "materials" anyway so that should be fine...
struct MaterialAsset
{
  AssetMetadata                     metadata;
  AssetRef<ShaderAsset>             shader;
  u32                               num_textures;
  OffsetPtr<AssetRef<TextureAsset>> textures;
};
ASSERT_SERIALIZABLE(MaterialAsset);


// Every model consists of multiple model subsets (ModelSubset) that each hold their own
// material/vertex/index data. This is so that from DCC you can export a single "model"
// that the engine will then de-construct into mesh instances that can be rendered separately.
struct ModelAsset
{
  struct ModelSubset
  {
    u64                     num_vertices;
    u64                     num_indices;
    AssetRef<MaterialAsset> material;
    OffsetPtr<VertexAsset>  vertices;
    OffsetPtr<u32>          indices;
  };

  struct Meshlet
  {
    u8 num_verts;
    u8 num_tris;
  };

  AssetMetadata          metadata;
  u64                    num_model_subsets;
  OffsetPtr<ModelSubset> model_subsets;
};
ASSERT_SERIALIZABLE(ModelAsset);
