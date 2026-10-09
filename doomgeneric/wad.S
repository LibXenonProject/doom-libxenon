/* Embedded IWAD, exposed to fopen() via xenon_main.c. */
        .section .rodata
        .balign 16
        .globl  wad_data
wad_data:
        .incbin "iwad.wad"
        .globl  wad_data_end
wad_data_end:
