#include "Core/Foundation/assets.h"

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
