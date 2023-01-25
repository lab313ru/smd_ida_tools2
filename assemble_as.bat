@echo off

set PATH=%PATH%;./bin/;

set pth=%~n1
set ext=%~x1

for %%i in (%pth%) do (set pth=%%~ni)

asw -L -olist %pth%_list.txt -o %pth%_new.p %1
p2bin %pth%_new.p %pth%_new.bin
del %pth%_new.p /q