@echo off

setlocal

set SLN=%ATHENA_ROOT%\vs\athena.sln

if "%1" == "Athena" set SLN=%ATHENA_ROOT%\vs\athena.sln

call devenv %SLN%
