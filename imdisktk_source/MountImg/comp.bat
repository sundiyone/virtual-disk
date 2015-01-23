\mingw32\bin\i686-w64-mingw32-windres.exe resource.rc "%TEMP%\resource.o"
\mingw32\bin\i686-w64-mingw32-gcc.exe MountImg.c "%TEMP%\resource.o" -o MountImg.exe -municode -mwindows -s -Os -Wall^
 -DMINGW_NOSTDLIB -nostdlib -lmsvcrt -lkernel32 -lshell32 -luser32 -ladvapi32 -lcomdlg32 -lshlwapi
del "%TEMP%\resource.o"
pause