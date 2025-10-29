@echo off

setlocal

set SLN=%ATHENA_ROOT%\vs\%1.sln

call devenv %SLN%
