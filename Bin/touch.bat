@echo off

if exist %1 (
  copy /B "%1"+,, "%1" > NUL
) else (
  type nul > %1
)
