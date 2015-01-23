\mingw32\bin\i686-w64-mingw32-windres.exe resource.rc "%TEMP%\resource.o"
\mingw32\bin\i686-w64-mingw32-gcc.exe RamDyn.c "%TEMP%\resource.o" -o RamDyn32.exe -municode -mwindows -s -O3 -minline-all-stringops -Wall^
 -DMINGW_NOSTDLIB -nostdlib -lmingwex -lgcc -lmsvcrt -lkernel32 -lshell32 -luser32 -lshlwapi -ladvapi32 -Wl,--large-address-aware
del "%TEMP%\resource.o"
pause