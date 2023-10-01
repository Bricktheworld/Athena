@echo off

@echo Building assets...

if "%~1"=="" goto error

set out_asset_dir=%1\assets\
set in_asset_dir=%~dp0..\assets

if not exist %out_asset_dir% md %out_asset_dir%
xcopy /s/y/q %in_asset_dir% %out_asset_dir% || goto copy_error

@echo Successfully built assets!
goto :eof

:error
@echo Usage: %0 ^<Output Dir^>
exit /B 1

:copy_error
@echo Failed to copy assets!
exit /B 1