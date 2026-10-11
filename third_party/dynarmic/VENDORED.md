# Vendored dynarmic

[dynarmic](https://github.com/azahar-emu/dynarmic) at commit
`a46601580d5512d324104f985b5f0209dc980ddc` (Azahar's fork), with its bundled
externals at the commits that dynarmic commit pins:

| External | Commit |
| --- | --- |
| [fmt](https://github.com/fmtlib/fmt) | `7b273fbb541c7c9058800bc613f4291274b2d26e` |
| [mcl](https://github.com/azahar-emu/mcl) | `5fc4beaf331037649b10625736b41365defb4f50` (0.1.14) |
| [oaknut](https://github.com/merryhime/oaknut) | `94c726ce0338b054eb8cb5ea91de8fe6c19f4392` |
| [robin-map](https://github.com/Tessil/robin-map) | `054ec5ad67440fcd65e0497e5a27ef31f53fcc7f` |

Only what Keyboard 72 builds is kept: the A32 frontend and the arm64 backend.
Removed: tests, docs, the A64 frontend (but for headers the arm64 backend
includes) and the x86_64 and RISC-V backends
(and the externals only they use: biscuit, catch, xbyak, zydis, zycore), and
the externals' own tests and docs. Every file kept is unchanged from those
commits, except for the changes listed below. Licenses: `LICENSE.txt`
(0BSD) and each external's `LICENSE`.

## Local changes

- `src/dynarmic/backend/arm64/address_space.{h,cpp}`: compact per-block
  bookkeeping (see that commit's message). It cut the keyboard's memory after
  typing on a test phone from 517 to 330 MB with unchanged typing times.
- `src/dynarmic/common/memory_pool.cpp`: freed IR instruction slabs (one per
  compiled block, a few hundred KiB each) are kept per thread for reuse. With
  GrapheneOS's hardened allocator every such allocation was a fresh mapping,
  and that alone more than doubled the engine's start-up time.
