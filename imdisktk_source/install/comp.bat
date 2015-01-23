\mingw32\bin\i686-w64-mingw32-windres.exe resource.rc "%TEMP%\resource.o"
\mingw32\bin\i686-w64-mingw32-gcc.exe setup.c "%TEMP%\resource.o" -o setup.exe -municode -mwindows -s -Os -Wall^
 -DMINGW_NOSTDLIB -nostdlib -lmsvcrt -lkernel32 -lshell32 -luser32 -ladvapi32 -lcomdlg32 -lgdi32 -lshlwapi -lole32 -luuid -lversion -lsetupapi
del "%TEMP%\resource.o"
pause