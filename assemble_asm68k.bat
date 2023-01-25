@echo off

set PATH=%PATH%;./bin/;

set pth=%~n1
set ext=%~x1

for %%i in (%pth%) do (set pth=%%~ni)

asm68k /p %1,%pth%_new.bin,,%pth%_list.txt