// Patch sites in the original engine (ELF offsets): original and replacement words.
constexpr EnginePatch kFloatDispatchPatches[] = {
    {0x003c9a98, 0xe28fc617, 0xe59fc000},  // __aeabi_fmul
    {0x003c9a9c, 0xe28ccaae, 0xea4d2f7a},  // __aeabi_fmul
    {0x003c9aa0, 0xe5bcf4c4, 0x01b87f64},  // __aeabi_fmul
    {0x01b77f64, 0x0172588c, 0x0172588c},  // __aeabi_fmul GOT guard
    {0x003c9aa4, 0xe28fc617, 0xe59fc000},  // __aeabi_fcmplt
    {0x003c9aa8, 0xe28ccaae, 0xea4d3060},  // __aeabi_fcmplt
    {0x003c9aac, 0xe5bcf4bc, 0x01b87f68},  // __aeabi_fcmplt
    {0x01b77f68, 0x01725c30, 0x01725c30},  // __aeabi_fcmplt GOT guard
    {0x003c9ab0, 0xe28fc617, 0xe59fc000},  // __aeabi_fcmpgt
    {0x003c9ab4, 0xe28ccaae, 0xea4d306c},  // __aeabi_fcmpgt
    {0x003c9ab8, 0xe5bcf4b4, 0x01b87f6c},  // __aeabi_fcmpgt
    {0x01b77f6c, 0x01725c6c, 0x01725c6c},  // __aeabi_fcmpgt GOT guard
    {0x003c9abc, 0xe28fc617, 0xe59fc000},  // __aeabi_i2f
    {0x003c9ac0, 0xe28ccaae, 0xea4d2f46},  // __aeabi_i2f
    {0x003c9ac4, 0xe5bcf4ac, 0x01b87f70},  // __aeabi_i2f
    {0x01b77f70, 0x017257e0, 0x017257e0},  // __aeabi_i2f GOT guard
    {0x003c9ac8, 0xe28fc617, 0xe59fc000},  // __aeabi_f2iz
    {0x003c9acc, 0xe28ccaae, 0xea4d3079},  // __aeabi_f2iz
    {0x003c9ad0, 0xe5bcf4a4, 0x01b87f74},  // __aeabi_f2iz
    {0x01b77f74, 0x01725cb8, 0x01725cb8},  // __aeabi_f2iz GOT guard
    {0x003ca3a4, 0xe28fc617, 0xe59fc000},  // __aeabi_fcmpge
    {0x003ca3a8, 0xe28ccaad, 0xea4d2e2a},  // __aeabi_fcmpge
    {0x003ca3ac, 0xe5bcfebc, 0x01b88268},  // __aeabi_fcmpge
    {0x01b78268, 0x01725c58, 0x01725c58},  // __aeabi_fcmpge GOT guard
    {0x003ca3b0, 0xe28fc617, 0xe59fc000},  // __aeabi_fcmpun
    {0x003ca3b4, 0xe28ccaad, 0xea4d2e31},  // __aeabi_fcmpun
    {0x003ca3b8, 0xe5bcfeb4, 0x01b8826c},  // __aeabi_fcmpun
    {0x01b7826c, 0x01725c80, 0x01725c80},  // __aeabi_fcmpun GOT guard
    {0x003ca3bc, 0xe28fc617, 0xe59fc000},  // __aeabi_f2d
    {0x003ca3c0, 0xe28ccaad, 0xea4d2aa7},  // __aeabi_f2d
    {0x003ca3c4, 0xe5bcfeac, 0x01b88270},  // __aeabi_f2d
    {0x01b78270, 0x01724e64, 0x01724e64},  // __aeabi_f2d GOT guard
    {0x003ca9c8, 0xe28fc617, 0xe59fc000},  // __aeabi_dadd
    {0x003ca9cc, 0xe28ccaad, 0xea4d2866},  // __aeabi_dadd
    {0x003ca9d0, 0xe5bcfaa4, 0x01b88474},  // __aeabi_dadd
    {0x01b78474, 0x01724b6c, 0x01724b6c},  // __aeabi_dadd GOT guard
    {0x003ca9d4, 0xe28fc617, 0xe59fc000},  // __aeabi_dcmplt
    {0x003ca9d8, 0xe28ccaad, 0xea4d2aa1},  // __aeabi_dcmplt
    {0x003ca9dc, 0xe5bcfa9c, 0x01b88478},  // __aeabi_dcmplt
    {0x01b78478, 0x01725464, 0x01725464},  // __aeabi_dcmplt GOT guard
    {0x003cab18, 0xe28fc617, 0xe59fc000},  // __aeabi_f2uiz
    {0x003cab1c, 0xe28ccaad, 0xea4d2c7c},  // __aeabi_f2uiz
    {0x003cab20, 0xe5bcf9c4, 0x01b884e4},  // __aeabi_f2uiz
    {0x01b784e4, 0x01725d14, 0x01725d14},  // __aeabi_f2uiz GOT guard
    {0x003cac14, 0xe28fc617, 0xe59fc000},  // __aeabi_fcmple
    {0x003cac18, 0xe28ccaad, 0xea4d2c09},  // __aeabi_fcmple
    {0x003cac1c, 0xe5bcf91c, 0x01b88538},  // __aeabi_fcmple
    {0x01b78538, 0x01725c44, 0x01725c44},  // __aeabi_fcmple GOT guard
    {0x003cac20, 0xe28fc617, 0xe59fc000},  // __aeabi_fsub
    {0x003cac24, 0xe28ccaad, 0xea4d2a86},  // __aeabi_fsub
    {0x003cac28, 0xe5bcf914, 0x01b8853c},  // __aeabi_fsub
    {0x01b7853c, 0x01725644, 0x01725644},  // __aeabi_fsub GOT guard
    {0x003cadd0, 0xe28fc617, 0xe59fc000},  // __aeabi_fadd
    {0x003cadd4, 0xe28ccaad, 0xea4d2a1b},  // __aeabi_fadd
    {0x003cadd8, 0xe5bcf7f4, 0x01b885cc},  // __aeabi_fadd
    {0x01b785cc, 0x01725648, 0x01725648},  // __aeabi_fadd GOT guard
    {0x003cae24, 0xe28fc617, 0xe59fc000},  // __aeabi_fdiv
    {0x003cae28, 0xe28ccaad, 0xea4d2afd},  // __aeabi_fdiv
    {0x003cae2c, 0xe5bcf7bc, 0x01b885e8},  // __aeabi_fdiv
    {0x01b785e8, 0x01725a24, 0x01725a24},  // __aeabi_fdiv GOT guard
    {0x003cb274, 0xe28fc617, 0xe59fc000},  // __aeabi_dcmpge
    {0x003cb278, 0xe28ccaad, 0xea4d2883},  // __aeabi_dcmpge
    {0x003cb27c, 0xe5bcf4dc, 0x01b88758},  // __aeabi_dcmpge
    {0x01b78758, 0x0172548c, 0x0172548c},  // __aeabi_dcmpge GOT guard
    {0x003cb280, 0xe28fc617, 0xe59fc000},  // __aeabi_dcmpun
    {0x003cb284, 0xe28ccaad, 0xea4d288a},  // __aeabi_dcmpun
    {0x003cb288, 0xe5bcf4d4, 0x01b8875c},  // __aeabi_dcmpun
    {0x01b7875c, 0x017254b4, 0x017254b4},  // __aeabi_dcmpun GOT guard
    {0x003cc0c0, 0xe28fc617, 0xe59fc000},  // __aeabi_dsub
    {0x003cc0c4, 0xe28ccaac, 0xea4d22a7},  // __aeabi_dsub
    {0x003cc0c8, 0xe5bcfb54, 0x01b88c1c},  // __aeabi_dsub
    {0x01b78c1c, 0x01724b68, 0x01724b68},  // __aeabi_dsub GOT guard
    {0x003cc0cc, 0xe28fc617, 0xe59fc000},  // __aeabi_d2f
    {0x003cc0d0, 0xe28ccaac, 0xea4d2531},  // __aeabi_d2f
    {0x003cc0d4, 0xe5bcfb4c, 0x01b88c20},  // __aeabi_d2f
    {0x01b78c20, 0x0172559c, 0x0172559c},  // __aeabi_d2f GOT guard
    {0x003cc0d8, 0xe28fc617, 0xe59fc000},  // __aeabi_ddiv
    {0x003cc0dc, 0xe28ccaac, 0xea4d2428},  // __aeabi_ddiv
    {0x003cc0e0, 0xe5bcfb44, 0x01b88c24},  // __aeabi_ddiv
    {0x01b78c24, 0x01725184, 0x01725184},  // __aeabi_ddiv GOT guard
    {0x003cc234, 0xe28fc617, 0xe59fc000},  // __aeabi_dmul
    {0x003cc238, 0xe28ccaac, 0xea4d2336},  // __aeabi_dmul
    {0x003cc23c, 0xe5bcfa5c, 0x01b88c98},  // __aeabi_dmul
    {0x01b78c98, 0x01724f18, 0x01724f18},  // __aeabi_dmul GOT guard
    {0x003cc264, 0xe28fc617, 0xe59fc000},  // __aeabi_dcmple
    {0x003cc268, 0xe28ccaac, 0xea4d2482},  // __aeabi_dcmple
    {0x003cc26c, 0xe5bcfa3c, 0x01b88ca8},  // __aeabi_dcmple
    {0x01b78ca8, 0x01725478, 0x01725478},  // __aeabi_dcmple GOT guard
    {0x003cc4d4, 0xe28fc617, 0xe59fc000},  // __aeabi_dcmpgt
    {0x003cc4d8, 0xe28ccaac, 0xea4d23f0},  // __aeabi_dcmpgt
    {0x003cc4dc, 0xe5bcf89c, 0x01b88d78},  // __aeabi_dcmpgt
    {0x01b78d78, 0x017254a0, 0x017254a0},  // __aeabi_dcmpgt GOT guard
    {0x003cc570, 0xe28fc617, 0xe59fc000},  // __aeabi_ui2f
    {0x003cc574, 0xe28ccaac, 0xea4d2497},  // __aeabi_ui2f
    {0x003cc578, 0xe5bcf834, 0x01b88dac},  // __aeabi_ui2f
    {0x01b78dac, 0x017257d8, 0x017257d8},  // __aeabi_ui2f GOT guard
    {0x003cc9d8, 0xe28fc617, 0xe59fc000},  // __aeabi_fcmpeq
    {0x003cc9dc, 0xe28ccaac, 0xea4d248e},  // __aeabi_fcmpeq
    {0x003cc9e0, 0xe5bcf544, 0x01b88f24},  // __aeabi_fcmpeq
    {0x01b78f24, 0x01725c1c, 0x01725c1c},  // __aeabi_fcmpeq GOT guard
    {0x003cdd70, 0xe28fc617, 0xe59fc000},  // __aeabi_d2iz
    {0x003cdd74, 0xe28ccaab, 0xea4d1ddc},  // __aeabi_d2iz
    {0x003cdd78, 0xe5bcf834, 0x01b895ac},  // __aeabi_d2iz
    {0x01b795ac, 0x017254ec, 0x017254ec},  // __aeabi_d2iz GOT guard
    {0x003cdd7c, 0xe28fc617, 0xe59fc000},  // __aeabi_i2d
    {0x003cdd80, 0xe28ccaab, 0xea4d1c2d},  // __aeabi_i2d
    {0x003cdd84, 0xe5bcf82c, 0x01b895b0},  // __aeabi_i2d
    {0x01b795b0, 0x01724e3c, 0x01724e3c},  // __aeabi_i2d GOT guard
    {0x003d3698, 0xe28fc617, 0xe59fc000},  // __aeabi_l2d
    {0x003d369c, 0xe28ccaa7, 0xea4d0605},  // __aeabi_l2d
    {0x003d36a0, 0xe5bcfcc4, 0x01b8b364},  // __aeabi_l2d
    {0x01b7b364, 0x01724eb8, 0x01724eb8},  // __aeabi_l2d GOT guard
    {0x003d7c40, 0xe28fc617, 0xe59fc000},  // __aeabi_ui2d
    {0x003d7c44, 0xe28ccaa4, 0xea4cf473},  // __aeabi_ui2d
    {0x003d7c48, 0xe5bcfe54, 0x01b8ca9c},  // __aeabi_ui2d
    {0x01b7ca9c, 0x01724e18, 0x01724e18},  // __aeabi_ui2d GOT guard
    {0x003d7c4c, 0xe28fc617, 0xe59fc000},  // __aeabi_d2uiz
    {0x003d7c50, 0xe28ccaa4, 0xea4cf63c},  // __aeabi_d2uiz
    {0x003d7c54, 0xe5bcfe4c, 0x01b8caa0},  // __aeabi_d2uiz
    {0x01b7caa0, 0x01725548, 0x01725548},  // __aeabi_d2uiz GOT guard
    {0x003e1bb8, 0xe28fc617, 0xe59fc000},  // __aeabi_dcmpeq
    {0x003e1bbc, 0xe28cca9e, 0xea4cce23},  // __aeabi_dcmpeq
    {0x003e1bc0, 0xe5bcf404, 0x01b8ffc4},  // __aeabi_dcmpeq
    {0x01b7ffc4, 0x01725450, 0x01725450},  // __aeabi_dcmpeq GOT guard
    {0x003e6268, 0xe28fc617, 0xe59fc000},  // __aeabi_ul2f
    {0x003e626c, 0xe28cca9b, 0xea4cbd63},  // __aeabi_ul2f
    {0x003e6270, 0xe5bcf4e4, 0x01b91754},  // __aeabi_ul2f
    {0x01b81754, 0x01725800, 0x01725800},  // __aeabi_ul2f GOT guard
    {0x003edf48, 0xe28fc617, 0xe59fc000},  // __aeabi_ul2d
    {0x003edf4c, 0xe28cca96, 0xea4c9bd4},  // __aeabi_ul2d
    {0x003edf50, 0xe5bcf1a4, 0x01b940f4},  // __aeabi_ul2d
    {0x01b840f4, 0x01724ea4, 0x01724ea4},  // __aeabi_ul2d GOT guard
    {0x0040eb04, 0xe28fc617, 0xe59fc000},  // __cmpdf2
    {0x0040eb08, 0xe28cca80, 0xea4c1a22},  // __cmpdf2
    {0x0040eb0c, 0xe5bcf47c, 0x01b9ef88},  // __cmpdf2
    {0x01b8ef88, 0x01725398, 0x01725398},  // __cmpdf2 GOT guard
    {0x0040eb10, 0xe28fc617, 0xe59fc000},  // __aeabi_cdcmple
    {0x0040eb14, 0xe28cca80, 0xea4c1a48},  // __aeabi_cdcmple
    {0x0040eb18, 0xe5bcf474, 0x01b9ef8c},  // __aeabi_cdcmple
    {0x01b8ef8c, 0x0172543c, 0x0172543c},  // __aeabi_cdcmple GOT guard
    {0x0040eb1c, 0xe28fc617, 0xe59fc000},  // __aeabi_cdrcmple
    {0x0040eb20, 0xe28cca80, 0xea4c1a3e},  // __aeabi_cdrcmple
    {0x0040eb24, 0xe5bcf46c, 0x01b9ef90},  // __aeabi_cdrcmple
    {0x01b8ef90, 0x01725420, 0x01725420},  // __aeabi_cdrcmple GOT guard
    {0x0040eb28, 0xe28fc617, 0xe59fc000},  // __cmpsf2
    {0x0040eb2c, 0xe28cca80, 0xea4c1c18},  // __cmpsf2
    {0x0040eb30, 0xe5bcf464, 0x01b9ef94},  // __cmpsf2
    {0x01b8ef94, 0x01725b94, 0x01725b94},  // __cmpsf2 GOT guard
    {0x0040eb34, 0xe28fc617, 0xe59fc000},  // __aeabi_cfcmple
    {0x0040eb38, 0xe28cca80, 0xea4c1c32},  // __aeabi_cfcmple
    {0x0040eb3c, 0xe5bcf45c, 0x01b9ef98},  // __aeabi_cfcmple
    {0x01b8ef98, 0x01725c08, 0x01725c08},  // __aeabi_cfcmple GOT guard
    {0x0040eb40, 0xe28fc617, 0xe59fc000},  // __aeabi_cfrcmple
    {0x0040eb44, 0xe28cca80, 0xea4c1c2b},  // __aeabi_cfrcmple
    {0x0040eb48, 0xe5bcf454, 0x01b9ef9c},  // __aeabi_cfrcmple
    {0x01b8ef9c, 0x01725bf8, 0x01725bf8},  // __aeabi_cfrcmple GOT guard
};
