# PicoGB

A small emulator libraray, which is capable to play Game Boy games, and a limited amount of Game Boy Color games.

PicoGB is designed to be easy to embed, and run as fast as possible, with the least amount of resource usage as possible.

## Embedding

See `pgb_main.h` for all the available functions to use.

The rest of the code has no dependencies, as it's the job of the embedder to implement callback functions not implemented in `fabric.c`.

## License

PicoGB is licensed under the GNU Lesser GPL v2.1, see LICENSE.txt
