\mingw32\bin\i686-w64-mingw32-windres.exe resource.rc "%TEMP%\resource.o"
\mingw32\bin\i686-w64-mingw32-gcc.exe RamDiskUI.c "%TEMP%\resource.o" -o RamDiskUI.exe -municode -mwindows -s -Os -Wall^
 -DMINGW_NOSTDLIB -nostdlib -lgcc -lmsvcrt -lntdll -lkernel32 -lshell32 -luser32 -ladvapi32 -lgdi32 -lcomctl32 -lcomdlg32 -lshlwapi
del "%TEMP%\resource.o"
pause