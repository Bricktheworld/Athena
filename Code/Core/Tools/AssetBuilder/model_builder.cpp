#include "Core/Tools/AssetBuilder/model_builder.h"
#include "Core/Tools/AssetBuilder/model_importer.h"

#include "Core/Tools/AssetBuilder/Vendor/meshoptimizer/meshoptimizer.h"

#if 0
static NaniteMesh
build_nanite_mesh(
  AllocHeap heap,
  const asset_builder::ImportedModelSubset& model
) {
  meshopt_Stream pos_stream = { .data = model.vertices, .size = sizeof(Vec3), .stride = sizeof(VertexAsset) };

  u32* remap = HEAP_ALLOC(u32, heap, model.num_vertices);
  meshopt_generateVertexRemapMulti(remap, model.indices, model.num_indices, model.num_vertices, &pos_stream, 1);

  auto clusterize = [&](const u32* indices)
  {
    const u32 kMaxVertices  = 192;
    const u32 kMaxTriangles = 128;
    const u32 kMinTriangles = (kMaxTriangles / 3) & ~3;
    const f32 kSplitFactor  = 2.0f;
    const f32 kFillWeight   = 0.75f;

    UNREFERENCED_PARAMETER(indices);

    UNREFERENCED_PARAMETER(kMaxVertices);
    UNREFERENCED_PARAMETER(kMaxTriangles);
    UNREFERENCED_PARAMETER(kMinTriangles);
    UNREFERENCED_PARAMETER(kSplitFactor);
    UNREFERENCED_PARAMETER(kFillWeight);
  };


  NaniteMesh ret;
  return ret;
}

#endif
