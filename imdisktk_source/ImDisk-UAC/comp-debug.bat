\mingw32\bin\i686-w64-mingw32-windres.exe resource.rc "%TEMP%\resource.o"
\mingw32\bin\i686-w64-mingw32-gcc.exe ImDisk-UAC.c "%TEMP%\resource.o" -o ImDisk-UAC.exe -lshlwapi -municode -s -Os -Wall
del "%TEMP%\resource.o"
pause