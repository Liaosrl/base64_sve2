# Base64 SVE2 Codec — Design Document

## Architecture

```
Encoder: ld1u8 → svtbl(pack) → bdep → revb → svtbl2/tbx → st1u8
Decoder: ld1u8 → svtbl2/nibble → revb → bext → svtbl(unpk) → st1u8
```

### Encoder Pipeline

Per u32 lane: 3 input bytes → 4 sextets → 4 B64 characters.

**A. Byte packing** (`svtbl`). Input is 3-byte groups. `svtbl` rearranges into `[B2,B1,B0,0]` per u32 lane for correct LE interpretation.

**B. `bdep`** (bit deposit, PDEP-equivalent). Scatters 24 contiguous bits into 4×6-bit groups:

```
bdep(packed, 0x3f3f3f3f):
  byte3[5:0] ← sextet[0]   byte2[5:0] ← sextet[1]
  byte1[5:0] ← sextet[2]   byte0[5:0] ← sextet[3]
```

**C. `revb`** (byte reverse). bdep output is `[s3,s2,s1,s0]` in LE. `svrevb_u32_x` swaps to `[s0,s1,s2,s3]`.

**D. B64 lookup** (`svtbl2` or `tbl+tbx`). Maps sextet 0–63 → ASCII character.

- `svtbl2_u8` (64-entry, 2-register): **1 instruction** when available.
- `tbl` + `tbx` (32-entry each, TBX out-of-range trick): **3 instructions** fallback.

### Decoder Pipeline

Per u32 lane: 4 B64 chars → 4 sextets → 3 output bytes.

**A. Char→sextet lookup** (`svtbl2` or nibble).

- **svtbl2 path**: Two 64-entry reverse tables. `svtbl2_u8(lo,chars)` covers 0–63, `svtbl2_u8(hi,chars-64)` covers 64–127. `svcmpge`+`svsel` merge.
- **Nibble path**: `sextet = tbl[char>>4] + (char & 0xF)`. Covers A-Z/a-z/0-9 with one TBL + ADD. `+` and `/` corrected by 2×`svsel`. Fallback for VL<256 or compilers without `svtbl2`.

**B. `revb` + `bext`** (bit extract, PEXT-equivalent). `revb` swaps sextet order. `bext(rev, 0x3f3f3f3f)` compacts 4×6 bits into 24 contiguous bits — **2 instructions** replacing 11 shift+add operations.

**C. Output packing** (`svtbl`). bext produces `[B0,B1,B2,0]` per u32. `svtbl` compacts to 3 consecutive bytes per group.

## svtbl2 VL Dependency

`svtbl2_u8` provides `2 × VL` entries:

| VL | Entries | Encoder (64-entry) | Decoder (128-entry) |
|----|---------|--------------------|---------------------|
| 128 bit (16B) | 32 | ❌ unsupported | ❌ unsupported |
| 256 bit (32B) | 64 | 1 table ✓ | 2 tables ✓ |
| 512 bit (64B) | 128 | 1 table ✓ | 1 table ✓ |

VL<256 is rejected at CMake time — both `svtbl2` and `tbl+tbx` fallback require
≥32-byte registers to cover 64 B64 entries. The CMake `check_c_source_runs`
verifies `svcntb() >= 32`; a FATAL_ERROR is raised on narrower hardware.
