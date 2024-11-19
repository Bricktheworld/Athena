# Athena D3D12 Renderer

Features:

- D3D12 rendering backend
- Render-graph architecture with automatic resource barriers and transient allocations
- Shader model 6.6 bindless rendering
- Real-time ray-traced global illumination using irradiance probes

[![Image](/Documentation/realtime_gi.gif)](https://www.youtube.com/watch?v=05GTN8YapZE)

[Video demo](https://www.youtube.com/watch?v=05GTN8YapZE)

# Setup

1. Run `setup.bat` from the root directory
2. Open `vs\athena.sln` in Visual Studio 2022
3. Build the full solution
4. Open a command prompt in the root of this repository and run 
  
    `.\vs\assetbuilder\output\win64\release\assetbuilder.exe Assets/Source/sponza/Sponza.gltf .`
   
   This will build the sponza model asset (you should see it in `Assets\Built`)
5. Set the startup project in visual studio to be `Engine`
6. Set the working directory in visual studio to be from the root of the repository
7. Run the project!

