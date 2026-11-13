// Noto Sans TTF files embedded in flash (rodata), rendered at runtime by LVGL TinyTTF.
// Paths are relative to the project directory (the compiler's working directory under PlatformIO;
// the simulator passes it to the assembler with -I). Shared with the PC simulator (sim/).

    .section .rodata.embedded_fonts, "a"
    .balign 4

    .global font_noto_regular_ttf
    .global font_noto_regular_ttf_end
font_noto_regular_ttf:
    .incbin "assets/fonts/NotoSans-Regular.ttf"
font_noto_regular_ttf_end:

    .balign 4
    .global font_noto_bold_ttf
    .global font_noto_bold_ttf_end
font_noto_bold_ttf:
    .incbin "assets/fonts/NotoSans-Bold.ttf"
font_noto_bold_ttf_end:

#if defined(__linux__) && defined(__x86_64__)
    // PC simulator: mark the stack non-executable (linker warning otherwise)
    .section .note.GNU-stack, "", @progbits
#endif
