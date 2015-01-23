\mingw32\bin\i686-w64-mingw32-windres.exe resource.rc "%TEMP%\resource.o"
\mingw32\bin\i686-w64-mingw32-gcc.exe RamDiskUI.c "%TEMP%\resource.o" -o RamDiskUI.exe -lntdll -lgdi32 -lcomctl32 -lcomdlg32 -lshlwapi -municode -s -Os -Wall
del "%TEMP%\resource.o"
pause