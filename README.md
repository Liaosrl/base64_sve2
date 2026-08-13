# Base64 SVE2 Codec

High-performance Base64 codec using ARM SVE2 bit-manipulation instructions.

## Quick Start

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/demo_base64    # correctness
./build/bench_base64   # performance
```

The CMakeLists.txt hardcodes `-march=armv8-a+sve2+sve2-bitperm`. For cross-compile or
alternative flags, pass `-DCMAKE_C_FLAGS=...` on the first cmake invocation. Native builds
auto-detect the lookup path; cross-builds default to `TBL2_VL256`. Override either with
`-DBASE64_SVE2_LOOKUP_MODE=AUTO|TBL2_VL256|TBL2_VL_GE512|FALLBACK`.

## API

```c
#include "base64_sve2.h"

size_t base64_encode_sve2(const uint8_t *src, size_t src_len,
                          char *dst, size_t dst_cap);
size_t base64_decode_sve2(const char *src, size_t src_len,
                          uint8_t *dst, size_t dst_cap);
```

## Performance

### SVE2 (Kunpeng 950, VL=256 bit / 32B, Clang 17)

Latest optimized build, default automatic `svtbl2` decoder path, pinned with
`taskset -c 0`:

| Size | Encode | Decode | vs Scalar Encode |
|------|--------|--------|------------------|
| 1 KB | 22.37 GB/s | 13.14 GB/s | 11.6x |
| 10 KB | 24.67 GB/s | **14.21 GB/s** | 12.3x |
| 50 KB | 24.20 GB/s | 13.77 GB/s | 12.5x |
| 100 KB | **24.69 GB/s** | 13.67 GB/s | 12.8x |
| 200 KB | 23.91 GB/s | 13.61 GB/s | 12.0x |
| 500 KB | 22.94 GB/s | 13.61 GB/s | 12.2x |
| 1 MB | 22.34 GB/s | 13.69 GB/s | 11.5x |

vs [Turbo-Base64](https://github.com/powturbo/Turbo-Base64) NEON on same hardware:

| | Size | Encode | Decode |
|---|---|---|---|
| **This (SVE2)** | 100KB | **24.69 GB/s** | **13.67 GB/s** |
| Turbo-Base64 (NEON) | 100KB | 15.8 GB/s | 10.4 GB/s |
| Advantage | | **+56%** | **+31%** |

### x86 — Turbo-Base64 (reference)

| Hardware | Implementation | Size | Encode | Decode |
|----------|----------------|------|--------|--------|
| AMD EPYC 9654 (Zen 4) | `tb64v512vbmi` (AVX-512 VBMI2) | 100KB | **~60 GB/s** | **~60 GB/s** |
| AMD EPYC 9654 (Zen 4) | `tb64v256` (AVX2) | 100KB | ~30 GB/s | ~32 GB/s |
| Intel i7-12700 | `tb64v256` (AVX2) | 100KB | 35.4 GB/s | 46.8 GB/s |

Note: This project implements ARM SVE2 only, reach [Turbo-Base64](https://github.com/powturbo/Turbo-Base64) for x86 SIMD implementation.

## Requirements

- GCC 12+ / Clang 17+ with `-march=armv8-a+sve2+sve2-bitperm`
- VL ≥ 256 bit (32-byte registers). VL=128 is unsupported — both `svtbl2` and `tbl+tbx` need ≥32B to cover the 64-entry B64 table.

## Design

See [doc/design.md](doc/design.md). Pipeline:

```
Encode: ld1u8 → svtbl(pack) → bdep → revb → svtbl2/tbx → st1u8
Decode: ld1u8 → svtbl2/nibble → revb → bext → svtbl(unpk) → st1u8
```

Key instructions:
- **`bdep`** (PDEP equiv): 3×8→4×6 scatter in **1 instruction**
- **`bext`** (PEXT equiv): 4×6→3×8 compact in **1 instruction** (replaces 11 shift+add)
- **`svtbl2_u8`**: 64-entry 2-register TBL for single-instruction lookup

## Files

```
base64/
├── CMakeLists.txt
├── base64_sve2.{h,c}
├── demo_base64.c / bench_base64.c
├── README.md
├── doc/
│   └── design.md
└── LICENSE
```
