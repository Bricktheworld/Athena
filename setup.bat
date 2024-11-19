@echo off
.\Bin\Sharpmake\Sharpmake.Application.exe /sources('athena.sharpmake.cs') || goto :error
if not exist "Code\Core\Engine\Generated\" mkdir Code\Core\Engine\Generated || goto :error
if not exist "Code\Core\Engine\Generated\Shaders\" mkdir Code\Core\Engine\Generated\Shaders || goto :error
if not exist "Assets\Built\" mkdir Assets\Built || goto :error
echo "Success! Press spacebar to close"
pause
goto :EOF

:error
echo Failed with error %errorlevel%
exit /b %errorlevel%

