// Patch sites in the original engine (ELF offsets): original and replacement words.
constexpr EnginePatch kRefcountAddPatches[] = {
    {0x017167b8, 0xe92d40f8, 0xe92d40f8},  // single-threaded fetch-add
    {0x017167bc, 0xe59f6028, 0xe1a02000},  // single-threaded fetch-add
    {0x017167c0, 0xe1a05000, 0xe5920000},  // single-threaded fetch-add
    {0x017167c4, 0xe1a07001, 0xe0801001},  // single-threaded fetch-add
    {0x017167c8, 0xe5954000, 0xe5821000},  // single-threaded fetch-add
    {0x017167cc, 0xe1a02005, 0xe1500000},  // single-threaded fetch-add
    {0x017167d0, 0xe1a00004, 0xe28fe004},  // single-threaded fetch-add
    {0x017167d4, 0xe0841007, 0xe8bd80f8},  // single-threaded fetch-add
    {0x017167d8, 0xe12fff36, 0xe12fff36},  // single-threaded fetch-add
    {0x017167dc, 0xe3500000, 0xe3500000},  // single-threaded fetch-add
    {0x017167e0, 0x1afffff8, 0x1afffff8},  // single-threaded fetch-add
    {0x017167e4, 0xe1a00004, 0xe1a00004},  // single-threaded fetch-add
    {0x017167e8, 0xe8bd80f8, 0xe8bd80f8},  // single-threaded fetch-add
    {0x017167ec, 0xffff0fc0, 0xffff0fc0},  // single-threaded fetch-add
    {0x01719524, 0xe92d41f0, 0xe92d41f0},  // single-threaded fetch-add
    {0x01719528, 0xe1a05000, 0xe1a02000},  // single-threaded fetch-add
    {0x0171952c, 0xe59f6034, 0xe5920000},  // single-threaded fetch-add
    {0x01719530, 0xe1a07001, 0xe0801001},  // single-threaded fetch-add
    {0x01719534, 0xe59f8030, 0xe5821000},  // single-threaded fetch-add
    {0x01719538, 0xe12fff36, 0xe3a03000},  // single-threaded fetch-add
    {0x0171953c, 0xe5954000, 0xe3530000},  // single-threaded fetch-add
    {0x01719540, 0xe1a00004, 0xe28fe018},  // single-threaded fetch-add
    {0x01719544, 0xe0841007, 0xe8bd81f0},  // single-threaded fetch-add
    {0x01719548, 0xe1a02005, 0xe1a02005},  // single-threaded fetch-add
    {0x0171954c, 0xe12fff38, 0xe12fff38},  // single-threaded fetch-add
    {0x01719550, 0xe3500000, 0xe3500000},  // single-threaded fetch-add
    {0x01719554, 0x15954000, 0x15954000},  // single-threaded fetch-add
    {0x01719558, 0x1afffff8, 0x1afffff8},  // single-threaded fetch-add
    {0x0171955c, 0xe12fff36, 0xe12fff36},  // single-threaded fetch-add
    {0x01719560, 0xe1a00004, 0xe1a00004},  // single-threaded fetch-add
    {0x01719564, 0xe8bd81f0, 0xe8bd81f0},  // single-threaded fetch-add
    {0x01719568, 0xffff0fa0, 0xffff0fa0},  // single-threaded fetch-add
    {0x0171956c, 0xffff0fc0, 0xffff0fc0},  // single-threaded fetch-add
};
