@echo off

@echo Building shaders...

if "%~1"=="" goto error

set dxc=%~dp0..\vendor\dxc\bin\x64\dxc.exe

set out_vertex_dir=%1\vertex\
set in_vertex_dir=%~dp0..\shaders\vertex

set out_pixel_dir=%1\pixel\
set in_pixel_dir=%~dp0..\shaders\pixel

set out_compute_dir=%1\compute\
set in_compute_dir=%~dp0..\shaders\compute

set out_ray_tracing_dir=%1\ray_tracing\
set in_ray_tracing_dir=%~dp0..\shaders\ray_tracing

if not exist %out_vertex_dir% md %out_vertex_dir%
if not exist %out_pixel_dir% md %out_pixel_dir%
if not exist %out_compute_dir% md %out_compute_dir%
if not exist %out_ray_tracing_dir% md %out_ray_tracing_dir%

for /f %%f in ('dir /b %in_vertex_dir%') do (%dxc% -T vs_6_6 -E main %in_vertex_dir%\%%f -Zi -Qembed_debug -Fo %out_vertex_dir%\%%f.bin || goto compilation_error)
for /f %%f in ('dir /b %in_pixel_dir%') do (%dxc% -T ps_6_6 -E main %in_pixel_dir%\%%f -Zi -Qembed_debug -Fo %out_pixel_dir%\%%f.bin || goto compilation_error)
for /f %%f in ('dir /b %in_compute_dir%') do (%dxc% -T cs_6_6 -E main %in_compute_dir%\%%f -Qembed_debug -Zi -Fo %out_compute_dir%\%%f.bin || goto compilation_error)
for /f %%f in ('dir /b %in_ray_tracing_dir%') do (%dxc% -T lib_6_6 %in_ray_tracing_dir%\%%f -Qembed_debug -Zi -Fo %out_ray_tracing_dir%\%%f.bin || goto compilation_error)

@echo Successfully compiled shaders!
goto :eof

:error
@echo Usage: %0 ^<Output Dir^>
exit /B 1

:compilation_error
@echo Failed to compile shader!
exit /B 1