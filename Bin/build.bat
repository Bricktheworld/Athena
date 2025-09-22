@echo off

setlocal

set PROJECT=Build

:GETOPTS
if /I "%1" == "-p" set PROJECT=%2 & shift
shift
if not "%1" == "" goto GETOPTS

call msbuild /t:%PROJECT% /p:Configuration="Release" /p:Platform="x64" /verbosity:minimal %ATHENA_ROOT%vs\athena.sln /m
