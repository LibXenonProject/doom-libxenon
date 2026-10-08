# genericdoom-libxenon

[Original README](./README.orig.md)

Minimal Doom for libxenon: [doomgeneric](https://github.com/ozkl/doomgeneric) (GPLv2)

Whole logic is located in `source/xenon_main.c`
Place `doom1.wad` in folder `wad/`

## Building

```
export DEVKITXENON=/usr/local/xenon
export LIBXENON_INC=$DEVKITXENON/usr/include
export LIBXENON_LIB=$DEVKITXENON/usr/lib
make -C doomgeneric -f Makefile.libxenon
```

Use `doomgeneric.elf32` renamed to `xenon.elf`.

## Limitations

- No sound/music
- No savegames/config
- Video: 320x200, no fullscreen
