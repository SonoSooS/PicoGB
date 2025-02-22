# PicoGB

A small emulator libraray, which is capable to play Game Boy games, and a limited amount of Game Boy Color games.

PicoGB is designed to be easy to embed, and run as fast as possible, with the least amount of resource usage as possible.

## Embedding

See `pgb_main.h` for all the available functions to use.

The rest of the code has no dependencies, as it's the job of the embedder to implement callback functions not implemented in `fabric.c`.

## Source code layout

Core:
- `microcode.c` - actual CPU implementation, dispatches reads and writes (using a cache where possible)
- `mi.c` - Memory Interface, currently only contains cache invalidation functions
- `ppu.c` - Picture Processing Unit, contains all code regarding graphics, including scanline rendering, interrupts, and IO register handling
- `apu.c` - Audio Processing Unit, contains all code regarding sound rendering, including IO register processing
- `fabric.c` - IO register processing, and certain callback functions pre-implemented for Game Boy system

Extensions:
- `profi.c` - gcc call stack profiler, used to find and optimize away function calls where inlining makes sense
- `lru.c` - Last Recently Used cache, used to implement fast ROM bank cache on low-RAM systems (like the RP2040 port)
- `popcounter.h` - line drawing algorithm implemented using a shift register

Executable:
- `test/winmain.c` - Windows executable, sound doesn't work in wine (causes a hang due to unsupported sample rate)

## Ports
- Windows - see `test/winmain.c`
- RP2040 - not publically released yet (available using VGA or SPI display output, broken audio)
- Playdate - not publically released yet (available using upscaling or not, audio is inaccurate and crashes sometimes)
- Game Boy Advanced - under construction

## Known limitations
- Instructions are not M-cycle accurate! Every instruction runs on the first M-cycle.  
  Instructions with immediate bytes (mostly affecting a8 and a16 types) execute too early.
  This also includes $CB opcodes, as they execute in the $CB byte instead.
- PPU is scanline-based.  
  Mid-scanline BGP/SCX/SCY/LCDC changes do not show up.
- Memory locking is not implemented.  
  It's kind of difficult implement with the memory area cache without lots of overhead.
- Instruction fetch at $FDFE with a16 or n16 immediate type will fetch the high byte of the immediate from out-of-bounds host memory.  
  This is by design as an optimization of the logic.
- CGB double-speed mode is not only broken, but it's also the responsibility of the frontend to implement properly.

## License

PicoGB is licensed under the GNU Lesser GPL v2.1, see LICENSE.txt
