#pragma once
#include "Core/Foundation/types.h"

struct VertexAsset;

struct NaniteMeshlet
{
  VertexAsset* vertices;
  u8*          indices;
  u32          index_count;
  u32          vertex_count;
};

struct NaniteMesh
{
};


  