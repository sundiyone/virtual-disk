rem \mingw64\bin\gcc.exe RamDyn.c -S -o RamDyn64.s -municode -mwindows -s -O3 -minline-all-stringops -Wall -DMINGW_NOSTDLIB
\mingw64\bin\windres.exe resource.rc "%TEMP%\resource.o"
\mingw64\bin\gcc.exe RamDyn.c "%TEMP%\resource.o" -o RamDyn64.exe -municode -mwindows -s -O3 -minline-all-stringops -Wall^
 -DMINGW_NOSTDLIB -nostdlib -lmsvcrt -lkernel32 -lshell32 -luser32 -lshlwapi -ladvapi32
del "%TEMP%\resource.o"
pause