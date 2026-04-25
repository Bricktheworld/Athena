#pragma once
#include "Core/Foundation/types.h"

// These are just some global constants that are used throughout the engine

static constexpr u32 kMaxAssets    = 0x2000;

static constexpr u32 kMaxDynamicSceneObjs = 0x500;
static constexpr u32 kMaxStaticSceneObjs = 0x1500;
static constexpr u32 kMaxSceneObjs = kMaxStaticSceneObjs + kMaxDynamicSceneObjs;

static constexpr u64 kVertexBufferSize = MiB(300);
static constexpr u64 kIndexBufferSize  = MiB(100);
