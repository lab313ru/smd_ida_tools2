@echo off

set PATH=%PATH%;./bin/;

set pth=%~n1
set ext=%~x1

for %%i in (%pth%) do (set pth=%%~ni)

vasmm68k_mot -Fbin -m68000 -no-opt -L %pth%_list.txt -noialign -o %pth%_new.bin %1