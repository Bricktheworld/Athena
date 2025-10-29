@echo off

set ls=dir
set ATHENA_ROOT=%~dp0

:: Add scripts folder to path (has things like build and vsgen)
set PATH=%ATHENA_ROOT%Bin;%ATHENA_ROOT%Bin\python-3.12.0;%PATH%

:: Move the user into the root directory in case they aren't already
cd %ATHENA_ROOT%

:: Run some setup commands
call vcdevcmd
call vsgen


:: Drop the user into a commandline if launched from the explorer
echo %cmdcmdline% | find /i "/c" >nul
if %errorlevel%==0 (
  cmd /k
)


