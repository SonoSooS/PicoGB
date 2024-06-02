@gcc -o tst.exe -g -Og -DPICOGB_CUSTOM=1 -I. ..\microcode.c ..\mi.c ..\ppu.c ..\apu.c ..\fabric.c winmain.c %* -lgdi32 -lwinmm

