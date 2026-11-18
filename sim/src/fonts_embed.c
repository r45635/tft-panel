// The Noto Sans files under the symbol names src/app/fonts.cpp expects, as the firmware's
// board_build.embed_files gives them. SIM_ASSETS is the absolute path of assets/fonts.
#if defined(__APPLE__)
#define SIM_RODATA ".const_data"
#else
#define SIM_RODATA ".section .rodata"
#endif
#define SIM_SYM(name) "_binary_assets_fonts_" name

#define SIM_EMBED(name, file)                                     \
    __asm__(SIM_RODATA "\n"                                       \
            ".globl " SIM_SYM(name) "_start\n"                    \
            ".globl " SIM_SYM(name) "_end\n"                      \
            SIM_SYM(name) "_start:\n"                             \
            ".incbin \"" SIM_ASSETS "/" file "\"\n"               \
            SIM_SYM(name) "_end:\n"                               \
            ".byte 0\n")

SIM_EMBED("NotoSans_400_ttf", "NotoSans-400.ttf");
SIM_EMBED("NotoSans_600_ttf", "NotoSans-600.ttf");
