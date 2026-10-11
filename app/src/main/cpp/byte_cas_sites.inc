// Patch sites in the original engine (ELF offsets): original and replacement words.
constexpr EnginePatch kByteCasPatches[] = {
    {0x017174bc, 0xe92d4ff8, 0xe92d4ff8},  // byte compare-exchange
    {0x017174c0, 0xe2004003, 0xe5d05000},  // byte compare-exchange
    {0x017174c4, 0xe1a04184, 0xe20160ff},  // byte compare-exchange
    {0x017174c8, 0xe3a050ff, 0xe1550006},  // byte compare-exchange
    {0x017174cc, 0xe1a05415, 0x1a00000a},  // byte compare-exchange
    {0x017174d0, 0xe59f804c, 0xe20280ff},  // byte compare-exchange
    {0x017174d4, 0xe0059412, 0xe2007003},  // byte compare-exchange
    {0x017174d8, 0xe1a06001, 0xe1a07187},  // byte compare-exchange
    {0x017174dc, 0xe3c07003, 0xe3a090ff},  // byte compare-exchange
    {0x017174e0, 0xe201b0ff, 0xe1a09719},  // byte compare-exchange
    {0x017174e4, 0xe1e0a005, 0xe3c02003},  // byte compare-exchange
    {0x017174e8, 0xe5970000, 0xe5921000},  // byte compare-exchange
    {0x017174ec, 0xe0003005, 0xe1c11009},  // byte compare-exchange
    {0x017174f0, 0xe1a03433, 0xe1811718},  // byte compare-exchange
    {0x017174f4, 0xe153000b, 0xe5821000},  // byte compare-exchange
    {0x017174f8, 0x120360ff, 0xe28fe010},  // byte compare-exchange
    {0x017174fc, 0x1a000005, 0xe6af0075},  // byte compare-exchange
    {0x01717500, 0xe00a1000, 0xe8bd8ff8},  // byte compare-exchange
    {0x01717504, 0xe1811009, 0xe1811009},  // byte compare-exchange
    {0x01717508, 0xe1a02007, 0xe1a02007},  // byte compare-exchange
    {0x0171750c, 0xe12fff38, 0xe12fff38},  // byte compare-exchange
    {0x01717510, 0xe3500000, 0xe3500000},  // byte compare-exchange
    {0x01717514, 0x1afffff3, 0x1afffff3},  // byte compare-exchange
    {0x01717518, 0xe1a00c06, 0xe1a00c06},  // byte compare-exchange
    {0x0171751c, 0xe1a00c40, 0xe1a00c40},  // byte compare-exchange
    {0x01717520, 0xe8bd8ff8, 0xe8bd8ff8},  // byte compare-exchange
    {0x01717524, 0xffff0fc0, 0xffff0fc0},  // byte compare-exchange
};
