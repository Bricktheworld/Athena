#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/math.h"
#include "Core/Foundation/hash.h"
#include "Core/Foundation/filesystem.h"

#include "Core/Foundation/Containers/array.h"

#include "Core/Engine/Render/render_graph.h"
#include "Core/Engine/Shaders/interlop.hlsli"

// TODO(Brandon): Don't duplicate this definition,
// we want some sort of shared header with this vertex definition...
//struct Vertex
//{
//  Vec3 position;
//  Vec3 normal;
//  Vec2 uv;
//};
static_assert(sizeof(Vertex) == sizeof(f32) * 8);

// Asset ID is a CRC32 hash of asset path...
typedef u32 AssetId;

template <typename T>
using AssetRef = AssetId;

static const u32 kAssetMagicNumber = crc32("ATHENA_ASSET", 12);

// The path is relative to the project root directory, so should be something like
// Assets/Source/model.fbx
// The asset ID is case insensitive
FOUNDATION_API AssetId path_to_asset_id(const char* path);
FOUNDATION_API fs::FileStream open_built_asset_file(AssetId asset);

enum struct TextureFormat 
{
};

enum struct ShaderType : u8
{
  kVertex,
  kPixel,
  kCompute,
};

enum struct AssetType : u32
{
  kModel,
  kTypeTexture,
  kShader,
  KMaterial,

  kCount,

  kUnknown,
};


struct MeshInstData
{
  Array<Vertex> vertices;
  Array<u32>    indices;
};

struct ModelData
{
  Array<MeshInstData> mesh_insts;
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

PACK_STRUCT_BEGIN()
struct AssetMetadata
{
  u32       magic_number;
  u32       version;
  AssetType asset_type;
  u32       asset_hash;
};

struct TextureAsset
{
  AssetMetadata metadata;
  TextureFormat format;
  u64           size;
  OffsetPtr<u8> data;
};

struct ShaderAsset
{
  AssetMetadata metadata;
  ShaderType    type;
  u64           size;
  OffsetPtr<u8> data;
};

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


// Every model consists of multiple mesh instances (MeshInst) that each hold their own
// material/vertex/index data. This is so that from DCC you can export a single "model"
// that the engine will then de-construct into mesh instances that can be rendered separately.
struct ModelAsset
{
  struct MeshInst
  {
    u64                     num_vertices;
    u64                     num_indices;
    AssetRef<MaterialAsset> material;
    OffsetPtr<Vertex>       vertices;
    OffsetPtr<u32>          indices;
  };

  AssetMetadata       metadata;
  u64                 num_mesh_insts;
  OffsetPtr<MeshInst> mesh_insts;
//  u64                 name_len;
//  OffsetPtr<char>     name;
};
PACK_STRUCT_END()
