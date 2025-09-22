@echo off
pushd %ATHENA_ROOT%
.\Bin\Sharpmake\Sharpmake.Application.exe /sources('athena.sharpmake.cs') || goto :error
if not exist "Code\Core\Engine\Generated\" mkdir Code\Core\Engine\Generated || goto :error
if not exist "Code\Core\Engine\Generated\Shaders\" mkdir Code\Core\Engine\Generated\Shaders || goto :error
if not exist "Assets\Built\" mkdir Assets\Built || goto :error
popd %ATHENA_ROOT%
echo Generated project files.
goto :EOF

:error
echo Failed to generate project files with error: %errorlevel%
pause
exit /b %errorlevel%

