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

    *dst-- = *ch++;
    *dst-- = *ch++;

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
  return open_file(path);
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

#define FAIL_ON_ASSET_LOAD
#ifdef FAIL_ON_ASSET_LOAD
#define VALIDATE_ASSET_SIZE(size, expected) ASSERT((size) >= (expected))
#else
#define VALIDATE_ASSET_SIZE(size, expected) if ((size) < (expected)) return AssetLoadResult::kErrCorrupted
#endif

AssetLoadResult
load_model(AllocHeap heap, const void* buffer, size_t size, ModelData* out_model)
{
  const u8* buf = (const u8*)buffer;

  size_t expected_size = sizeof(ModelAsset);

  VALIDATE_ASSET_SIZE(size, expected_size); 

  ModelAsset*           model_asset = (ModelAsset*)buf;
  if (model_asset->metadata.asset_type != AssetType::kModel)
  {
    return AssetLoadResult::kErrMismatchedAssetType;
  }
  if (model_asset->metadata.magic_number != kAssetMagicNumber)
  {
    return AssetLoadResult::kErrCorrupted;
  }

  ModelAsset::ModelSubset* model_subsets  = (ModelAsset::ModelSubset*)(buf + model_asset->model_subsets);

  expected_size += sizeof(ModelAsset::ModelSubset) * model_asset->num_model_subsets;
  VALIDATE_ASSET_SIZE(size, expected_size);

  out_model->model_subsets = init_array<ModelSubsetData>(heap, model_asset->num_model_subsets);

  for (u32 imesh_inst = 0; imesh_inst < model_asset->num_model_subsets; imesh_inst++)
  {
    ModelAsset::ModelSubset* src = model_subsets + imesh_inst;
    ModelSubsetData* dst = array_add(&out_model->model_subsets);

    u64 vertex_buffer_size = src->num_vertices * sizeof(VertexAsset);
    u64 index_buffer_size  = src->num_indices  * sizeof(u32);
    VALIDATE_ASSET_SIZE(size, src->vertices + vertex_buffer_size);
    VALIDATE_ASSET_SIZE(size, src->indices  + index_buffer_size);

    dst->vertices = init_array_uninitialized<VertexAsset>(heap, src->num_vertices);
    dst->indices  = init_array_uninitialized<u32>        (heap, src->num_indices);
    dst->material = src->material;

    const u8* vertex_buffer = buf + src->vertices;
    const u8* index_buffer  = buf + src->indices;
    memcpy(dst->vertices.memory, vertex_buffer, vertex_buffer_size);
    memcpy(dst->indices.memory,  index_buffer,  index_buffer_size);
  }

  return AssetLoadResult::kOk;
}

void
dump_model_info(const ModelData& model)
{
  dbgln("Model: %lu mesh insts", model.model_subsets.size);
  for (u32 imesh_inst = 0; imesh_inst < model.model_subsets.size; imesh_inst++)
  {
    const ModelSubsetData* mesh_inst = &model.model_subsets[imesh_inst];
    dbgln(
      "\tMeshInst[%lu]: %lu vertices, %lu indices ",
      imesh_inst,
      mesh_inst->vertices.size,
      mesh_inst->indices.size
    );

    for (u32 ivertex = 0; ivertex < mesh_inst->vertices.size; ivertex++)
    {
      const VertexAsset* vertex = &mesh_inst->vertices[ivertex];
      dbgln(
        "\t\t[%u]{position: (%f,%f,%f), normal: (%f,%f,%f), uv: (%f,%f)}",
        ivertex,
        vertex->position.x,
        vertex->position.y,
        vertex->position.z,
        vertex->normal.x,
        vertex->normal.y,
        vertex->normal.z,
        vertex->uv.x,
        vertex->uv.y
      );
    }

    for (u32 index : mesh_inst->indices)
    {
      dbg("%u, ", index);
    }
    dbg("\n");
  }
}
