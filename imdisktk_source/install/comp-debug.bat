\mingw32\bin\i686-w64-mingw32-windres.exe resource.rc "%TEMP%\resource.o"
\mingw32\bin\i686-w64-mingw32-gcc.exe setup.c "%TEMP%\resource.o" -o setup.exe -municode -s -Os -Wall^
 -lcomdlg32 -lgdi32 -lshlwapi -lole32 -luuid -lversion -lsetupapi
del "%TEMP%\resource.o"
pause