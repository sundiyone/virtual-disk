\mingw32\bin\i686-w64-mingw32-windres.exe resource.rc "%TEMP%\resource.o"
\mingw32\bin\i686-w64-mingw32-gcc.exe ImDisk-UAC.c "%TEMP%\resource.o" -o ImDisk-UAC.exe -municode -mwindows -s -Os -Wall^
 -DMINGW_NOSTDLIB -nostdlib -lmsvcrt -lkernel32 -lshell32 -luser32 -lshlwapi
del "%TEMP%\resource.o"
pause