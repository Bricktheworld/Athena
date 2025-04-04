# Athena D3D12 Renderer

[Features](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#overview):

- [D3D12 rendering backend](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#d3d12-gpu-device)
- [Render-graph architecture with automatic resource barriers and transient allocations](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#render-graph)
- [Direct Storage streaming of assets to GPU memory](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#direct-storage)
- [Real-time ray-traced global illumination using irradiance probes](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#dynamic-diffuse-global-illumination)
- [Temporal Anti-Aliasing](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#temporal-anti-aliasing)
- [Depth of field bokeh blur](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#depth-of-field)
- [Asset builder](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#asset-builder)
- [Shader model 6.6 bindless rendering](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#bindless-descriptors)
- [HDR display output](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#hdr-display-output)
- [Deferred Shading](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#deferred-shading)
- [Multi-threaded asset streaming](https://github.com/Bricktheworld/Athena?tab=readme-ov-file#direct-storage)

[![Image](/Documentation/realtime_gi.gif)](https://www.youtube.com/watch?v=6I0wsOAcF3E)
[![Image](/Documentation/sponza.png)](https://www.youtube.com/watch?v=6I0wsOAcF3E)

[Video demo](https://www.youtube.com/watch?v=6I0wsOAcF3E)

# Requirements
- Git LFS
- Visual Studio 2022
- x64 capable machine on windows 10 or higher

# Build from Source

1. Run `setup.bat` from the root directory
2. Open `vs\athena.sln` in Visual Studio 2022
3. Build the full solution
4. Open a command prompt in the root of this repository and run 
  
    `.\vs\assetbuilder\output\win64\release\assetbuilder.exe Assets/Source/sponza/Sponza.gltf .`
   
   This will build the sponza model asset (you should see it in `Assets\Built`)
5. Set the startup project in visual studio to be `Engine`
6. Set the working directory in visual studio to be from the root of the repository
7. Run the project! (controls are wasdeq + right mouse click to move around)

# Pre-built Binary

Alternatively, there are pre-built binaries available in the [Releases](https://github.com/Bricktheworld/Athena/releases)
page.


# A trip through Athena's graphics pipeline

## Overview
The engine is split into multiple projects: 
- Foundation: contains shared implementations like hashtables, allocators, etc.
- AssetBuilder: responsible for cooking assets into built runtime format (models, textures, materials)
- Engine: the engine runtime which streams all of the built assets and renders the viewer

## D3D12 GPU device
I have a GPU interface that abstracts the D3D12 backend into something more generic. It still is a very low-level
abstraction that uses command buffers, allocators, submission queues, etc. but it is one small layer above the graphics
API. Hopefully this design will allow easy support for other backends like Vulkan and Metal.
- [graphics.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/graphics.h)
- [graphics.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/graphics.cpp)

## Render Graph
A declarative, C++ driven render graph is used to manage resource barriers and pass ordering. The graph is compiled once
at engine initilization and executed every frame. This allows GPU resources, descriptors, and barriers to all be
pre-computed so during execution, passing a descriptor to a shader does not require any hash-table lookups, virtual
command buffer filling, or a resolve stage to convert virtual handle IDs to physical ones. This improves debuggability
and performance as everything gets pre-computed (even resource barriers between dependency levels!). If you submit a bad
command, the validation layers will catch it and throw an error in the render handler function directly (not in a resolve 
pass as is common in most engines).

My graph also supports first class temporal resources, so creating/accessing temporal data is as easy as putting an extra
frame lifetime parameter in the initialization function. This also makes CPU writeable buffers much simpler by design.
- [render_graph.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/render_graph.h)
- [render_graph.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/render_graph.cpp)

## Direct Storage
Assets are baked directly into GPU usable formats during the asset cook process. This allows DirectStorage streaming of
assets directly into GPU memory. Although on PC this still necessitates copying to user-space memory, as 
[AMD shows](https://gpuopen.com/gdc-presentations/2023/GDC-2023-DirectStorage-optimizing-load-time-and-streaming.pdf) this
can still invoke up to 3 copies using traditional win32 APIs, whereas DirectStorage can bypass these mechanisms.

Asset streaming occurs on a separate thread from the main thread. `kick_asset_load` requests will push AssetId load requests
to the thread queue and these load requests will be consumed and processed (allocating memory, initialization etc.), then
resolved once DirectStorage signals the GPU fence. The thread will also sleep if no asset loads are in progress to save power.

- [asset_streaming.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/asset_streaming.h)
- [asset_streaming.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/asset_streaming.cpp)

## Dynamic Diffuse Global Illumination
Heavily based off of Nvidia's DDGI, I developed a similar, probe-based global illumination system that uses ray-tracing
to sample the diffuse irradiance of the environment, store into a octahedral compressed texture, and use for irradiance
calculations in the lighting pass.

- [ddgi.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/ddgi.h)
- [ddgi.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/ddgi.cpp)
- [ddgi.csh](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Shaders/DDGI/ddgi.csh)
- [probe_trace.rtsh](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Shaders/DDGI/probe_trace.rtsh)

## Temporal Anti-Aliasing
Accumulation based Temporal AA is implemented using a compute shader to reproject the previous frame's AA buffer onto the
current frame with neighborhood clamping rejection techniques. I also use Catmull-Rom filtering during reprojection in place
of bilinear filtering as it produces less blurry results in motion. I also leverage the first-class temporal resources in
the render graph to sample from the previous frame's AA and velocity buffers. I also luma-weight color samples to allow for
TAA to appear before tonemapping while still appearing stable and avoiding aliasing due to high contrast HDR values.

- [taa.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/taa.h)
- [taa.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/taa.cpp)
- [taa.csh](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Shaders/PostProcessing/taa.csh)

## Depth of Field
Depth of field is implemented using a multi-pass technique which downsamples the render buffers to half-res and then applies
a disc kernel based on the golden ratio sun-flower pattern. This creates a pleasant low-discrepancy sequence to sample from.
A scatter-as-gather approach is used where a heuristic of depth and CoC is used to determine how much samples in a fixed
sampling radius should contribute to a given texel (objects in the foreground should blur over objects in the background).

- [depth_of_field.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/depth_of_field.h)
- [depth_of_field.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/depth_of_field.cpp)
- [depth_of_field.csh](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Shaders/PostProcessing/depth_of_field.csh)

## Deferred Shading
I use a traditional GBuffer rendering pipeline that supports PBR textures/materials. The GBuffer pass uses a material shader
which outputs to several GBuffers, and a lighting pass reads the GBuffer data to light the HDR buffer which is then passed
through post processing. The material shader also supports alpha tested textures and will discard pixels that are below the
alpha threshold.

- [gbuffer.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/gbuffer.h)
- [gbuffer.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Render/gbuffer.cpp)
- [basic_normal_gloss.psh](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Shaders/Materials/basic_normal_gloss.psh)
- [standard_brdf.rtsh](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Shaders/Lighting/standard_brdf.rtsh)

## Bindless Descriptors
Shader model 6.6 enables full bindless descriptor heap indexing for both improved performance and usability. I was heavily
inspired by the PS5's capabilities of conjuring descriptors without having to mess with pools/allocators and just passing
pointers to shaders using SRTs (shader resource tables). I wanted to get as close to this experience as possible so I
have "Ptr" versions of every type of descriptor HLSL takes (Texture2DPtr, RWTexture2DPtr, etc.). Then, on the C++ side,
I simply have an operator to convert a render graph descriptor into one of these shader types which on the HLSL side are
just `uint32_t`s. This allows me to have what amounts to full bindless where I can pass any descriptor I want with ease into
the root constants of every shader (all of which share a single root signature).

- [root_signature.hlsli](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Shaders/root_signature.hlsli)
- [interlop.hlsli](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Shaders/interlop.hlsli)

## HDR Display Output
The swap chain uses HDR10 output with Rec. 2020 color space and PQ transfer function. Tonemapping uses a fixed ACES approximation
of 1000 nits max luminance and correctly applies the PQ transfer function.

- [tonemapping.psh](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Engine/Shaders/PostProcessing/tonemapping.psh)

## Asset Builder
The asset builder reads in model files using Assimp and finds material/texture properties, appropriately mapping them all into
built asset formats which can be easily consumed by the runtime. The assets are built into binary formats which are then read
directly to memory by the engine (targetting any modern machines means endianness is a non-issue).

No string paths are used in the engine for assets, and instead CRC32 hashes are used to make finding built asset files simple
and fast.

- [model_importer.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Tools/AssetBuilder/model_importer.h)
- [model_importer.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Tools/AssetBuilder/model_importer.cpp)
- [texture_importer.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Tools/AssetBuilder/texture_importer.h)
- [texture_importer.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Tools/AssetBuilder/texture_importer.cpp)
- [material_importer.h](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Tools/AssetBuilder/material_importer.h)
- [material_importer.cpp](https://github.com/Bricktheworld/Athena/blob/master/Code/Core/Tools/AssetBuilder/material_importer.cpp)

