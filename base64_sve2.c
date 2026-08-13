/* Base64 SVE2 codec -- bdep/bext + svtbl2/tbl */
#include "base64_sve2.h"
#include <arm_sve.h>
#include <string.h>

#if defined(BASE64_SVE2_USE_TBL2_VL256) && \
    defined(BASE64_SVE2_USE_TBL2_VL_GE512)
#error "TBL2 vector-length paths are mutually exclusive"
#endif

static const char B64[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* svtbl pack index: 3-byte groups -> [B2,B1,B0,0] per u32 */
static uint8_t pack_raw[256] __attribute__((aligned(16)));
static int     idx_ready = 0;

static void init_pack(void) {
    if (idx_ready) return;
    size_t vl = svcntb(), ng = vl / 4;
    for (size_t i = 0; i < ng; i++) {
        pack_raw[4*i+0] = (uint8_t)(3*i+2);
        pack_raw[4*i+1] = (uint8_t)(3*i+1);
        pack_raw[4*i+2] = (uint8_t)(3*i+0);
        pack_raw[4*i+3] = (uint8_t)vl;
    }
    idx_ready = 1;
}

/* ---- Encoder: ld -> tbl(pack) -> bdep -> revb -> tbl2/tbx -> st ---- */

size_t base64_encode_sve2(const uint8_t *src, size_t src_len,
                          char *dst, size_t dst_cap) {
    size_t need = ((src_len + 2) / 3) * 4;
    if (dst_cap < need) return 0;
    init_pack();
    size_t n_groups = src_len / 3, rem = src_len % 3;
    size_t vl_b = svcntb(), ng = vl_b / 4, nf = (n_groups / ng) * ng;

    svbool_t  p8    = svptrue_b8(), p32 = svptrue_b32();
    svbool_t  p_ld  = svwhilelt_b8((uint64_t)0, (uint64_t)(3 * ng));
    svuint32_t mask = svdup_u32(0x3f3f3f3f);
    svuint8_t pack  = svld1_u8(p8, pack_raw);

#if defined(BASE64_SVE2_USE_TBL2_VL256)
    svuint8x2_t b64tbl = svcreate2_u8(
        svld1_u8(p8, (const uint8_t*)B64),
        svld1_u8(p8, (const uint8_t*)B64 + 32));
#elif defined(BASE64_SVE2_USE_TBL2_VL_GE512)
    svbool_t p_b64 = svwhilelt_b8((uint64_t)0, (uint64_t)64);
    svuint8x2_t b64tbl = svcreate2_u8(
        svld1_u8(p_b64, (const uint8_t*)B64), svdup_u8(0));
#else
    svbool_t p_b64 = svwhilelt_b8((uint64_t)0, (uint64_t)64);
    svbool_t p_b64_hi = svwhilelt_b8((uint64_t)0, (uint64_t)32);
    svuint8_t b64_lo = svld1_u8(p_b64, (const uint8_t*)B64);
    svuint8_t b64_hi = svld1_u8(p_b64_hi, (const uint8_t*)B64 + 32);
    svuint8_t c31    = svdup_u8(0x1F);
#endif

    /* main loop: full chunks, 2x unrolled */
    size_t g;
    size_t nf2 = (nf / (2 * ng)) * (2 * ng);
    for (g = 0; g < nf2; g += 2 * ng) {
        svuint8_t  raw       = svld1_u8(p_ld, src + g*3);
        svuint8_t  raw1      = svld1_u8(p_ld, src + (g + ng)*3);
        svuint32_t packed    = svreinterpret_u32_u8(svtbl(raw, pack));
        svuint32_t packed1   = svreinterpret_u32_u8(svtbl(raw1, pack));
        svuint32_t scattered = svbdep_u32(packed, mask);
        svuint32_t scattered1= svbdep_u32(packed1, mask);
        svuint32_t reversed  = svrevb_u32_x(p32, scattered);
        svuint32_t reversed1 = svrevb_u32_x(p32, scattered1);
        svuint8_t  sextets   = svreinterpret_u8_u32(reversed);
        svuint8_t  sextets1  = svreinterpret_u8_u32(reversed1);
#if defined(BASE64_SVE2_USE_TBL2_VL256) || \
    defined(BASE64_SVE2_USE_TBL2_VL_GE512)
        svuint8_t  result  = svtbl2_u8(b64tbl, sextets);
        svuint8_t  result1 = svtbl2_u8(b64tbl, sextets1);
#else
        svuint8_t  result  = svtbl(b64_hi, svand_u8_x(p8, sextets, c31));
        result  = svtbx_u8(result,  b64_lo, sextets);
        svuint8_t  result1 = svtbl(b64_hi, svand_u8_x(p8, sextets1, c31));
        result1 = svtbx_u8(result1, b64_lo, sextets1);
#endif
        svst1_u8(p8, (uint8_t*)(dst + g*4), result);
        svst1_u8(p8, (uint8_t*)(dst + (g + ng)*4), result1);
    }
    /* remainder full chunks */
    for (; g < nf; g += ng) {
        svuint8_t  raw       = svld1_u8(p_ld, src + g*3);
        svuint32_t packed    = svreinterpret_u32_u8(svtbl(raw, pack));
        svuint32_t scattered = svbdep_u32(packed, mask);
        svuint32_t reversed  = svrevb_u32_x(p32, scattered);
        svuint8_t  sextets   = svreinterpret_u8_u32(reversed);
#if defined(BASE64_SVE2_USE_TBL2_VL256) || \
    defined(BASE64_SVE2_USE_TBL2_VL_GE512)
        svuint8_t  result = svtbl2_u8(b64tbl, sextets);
#else
        svuint8_t  result = svtbl(b64_hi, svand_u8_x(p8, sextets, c31));
        result = svtbx_u8(result, b64_lo, sextets);
#endif
        svst1_u8(p8, (uint8_t*)(dst + g*4), result);
    }

    /* tail: partial chunk */
    if (g < n_groups) {
        size_t    tail   = n_groups - g;
        svbool_t  p_tail = svwhilelt_b8((uint64_t)0, (uint64_t)(tail*3));
        svuint8_t raw    = svld1_u8(p_tail, src + g*3);
        svuint32_t pkd   = svbdep_u32(
            svreinterpret_u32_u8(svtbl(raw, pack)), mask);
        svuint8_t sxt    = svreinterpret_u8_u32(svrevb_u32_x(p32, pkd));
#if defined(BASE64_SVE2_USE_TBL2_VL256) || \
    defined(BASE64_SVE2_USE_TBL2_VL_GE512)
        svuint8_t result = svtbl2_u8(b64tbl, sxt);
#else
        svuint8_t result = svtbl(b64_hi, svand_u8_x(p8, sxt, c31));
        result = svtbx_u8(result, b64_lo, sxt);
#endif
        svst1_u8(svwhilelt_b8(0u, (uint32_t)(tail*4)),
                 (uint8_t*)(dst + g*4), result);
        g = n_groups;
    }

    /* scalar remainder + padding */
    if (rem) {
        uint32_t v = (rem == 1) ? ((uint32_t)src[g*3] << 16)
                     : (((uint32_t)src[g*3] << 16) |
                        ((uint32_t)src[g*3+1] << 8));
        size_t d = g * 4;
        dst[d+0] = B64[(v>>18)&0x3F]; dst[d+1] = B64[(v>>12)&0x3F];
        dst[d+2] = (rem == 1) ? '=' : B64[(v>>6)&0x3F]; dst[d+3] = '=';
    }
    return need;
}

/* ---- Decoder: ld -> tbl2/nibble -> revb -> bext -> tbl(unpk) -> st ---- */

#if !defined(BASE64_SVE2_USE_TBL2_VL256) && \
    !defined(BASE64_SVE2_USE_TBL2_VL_GE512)
static void build_hi_nibble(uint8_t t[32]) {
    memset(t, 0, 32);
    t[3]=52; t[4]=255; t[5]=15; t[6]=25; t[7]=41;
    /* '+' '/' handled by svsel */
}
#endif

size_t base64_decode_sve2(const char *src, size_t src_len,
                          uint8_t *dst, size_t dst_cap) {
    size_t n_groups = src_len / 4;
    size_t max_out = n_groups * 3;
    if (dst_cap < max_out) return 0;
    size_t vl_b = svcntb(), ng = vl_b / 4, nf = (n_groups / ng) * ng;
    svbool_t p8 = svptrue_b8(), p32 = svptrue_b32();
    svuint32_t bm = svdup_u32(0x3f3f3f3f);

#if defined(BASE64_SVE2_USE_TBL2_VL256) || \
    defined(BASE64_SVE2_USE_TBL2_VL_GE512)
    static const uint8_t DECTAB[128] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,62,0,0,0,63,52,53,54,55,56,57,58,59,60,61,0,0,0,0,0,0,
        0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,0,0,0,0,0,
        0,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,0,0,0,0,0,
    };
#  if defined(BASE64_SVE2_USE_TBL2_VL_GE512)
    size_t dec_first = vl_b < 128 ? vl_b : 128;
    svuint8_t dec_lo = svld1_u8(
        svwhilelt_b8((uint64_t)0, (uint64_t)dec_first), DECTAB);
    svuint8_t dec_hi = svdup_u8(0);
    if (vl_b < 128) {
        dec_hi = svld1_u8(
            svwhilelt_b8((uint64_t)0, (uint64_t)(128 - vl_b)),
            DECTAB + vl_b);
    }
    svuint8x2_t rt_full = svcreate2_u8(dec_lo, dec_hi);
#  else
    svuint8x2_t rt_lo = svcreate2_u8(
        svld1_u8(p8, DECTAB), svld1_u8(p8, DECTAB + 32));
    svuint8x2_t rt_hi = svcreate2_u8(
        svld1_u8(p8, DECTAB + 64), svld1_u8(p8, DECTAB + 96));
    svuint8_t c64 = svdup_u8(64);
#  endif
#else
    static uint8_t ht[32]; static int ht_ok = 0;
    if (!ht_ok) { build_hi_nibble(ht); ht_ok = 1; }
    svuint8_t t_hi = svld1_u8(
        svwhilelt_b8((uint64_t)0, (uint64_t)32), ht);
    svuint8_t c0f = svdup_u8(0x0F), cp = svdup_u8('+'), cs = svdup_u8('/');
    svuint8_t c62 = svdup_u8(62), c63 = svdup_u8(63);
#endif

    /* compact output tbl: 4 -> 3 bytes per group */
    static uint8_t unpk[256] __attribute__((aligned(16))); static int u_ok = 0;
    if (!u_ok) {
        size_t vl = svcntb(), ng = vl / 4;
        for (size_t i = 0; i < ng; i++)
            unpk[3*i+0] = (uint8_t)(4*i+2), unpk[3*i+1] = (uint8_t)(4*i+1),
            unpk[3*i+2] = (uint8_t)(4*i+0);
        for (size_t i = 3*ng; i < vl; i++) unpk[i] = (uint8_t)vl;
        u_ok = 1;
    }
    svuint8_t un_idx = svld1_u8(p8, unpk);
    svbool_t p_st = svwhilelt_b8((uint64_t)0, (uint64_t)(3 * ng));

    /* main loop: full chunks, 2x unrolled */
    size_t g;
    size_t nf2 = (nf / (2 * ng)) * (2 * ng);
    for (g = 0; g < nf2; g += 2 * ng) {
        svuint8_t chars  = svld1_u8(p8, (const uint8_t*)(src + g*4));
        svuint8_t chars1 = svld1_u8(p8, (const uint8_t*)(src + (g + ng)*4));
#if defined(BASE64_SVE2_USE_TBL2_VL_GE512)
        svuint8_t val  = svtbl2_u8(rt_full, chars);
        svuint8_t val1 = svtbl2_u8(rt_full, chars1);
#elif defined(BASE64_SVE2_USE_TBL2_VL256)
        svuint8_t r0  = svtbl2_u8(rt_lo, chars);
        svuint8_t r1  = svtbl2_u8(rt_hi, svsub_u8_x(p8, chars, c64));
        svuint8_t val = svsel_u8(svcmpge_u8(p8, chars, c64), r1, r0);
        svuint8_t r01 = svtbl2_u8(rt_lo, chars1);
        svuint8_t r11 = svtbl2_u8(rt_hi, svsub_u8_x(p8, chars1, c64));
        svuint8_t val1= svsel_u8(svcmpge_u8(p8, chars1, c64), r11, r01);
#else
        svuint8_t val  = svadd_u8_x(p8,
            svtbl(t_hi, svlsr_n_u8_x(p8, chars, 4)),
            svand_u8_x(p8, chars, c0f));
        val  = svsel_u8(svcmpeq_u8(p8, chars,  cp), c62, val);
        val  = svsel_u8(svcmpeq_u8(p8, chars,  cs), c63, val);
        svuint8_t val1 = svadd_u8_x(p8,
            svtbl(t_hi, svlsr_n_u8_x(p8, chars1, 4)),
            svand_u8_x(p8, chars1, c0f));
        val1 = svsel_u8(svcmpeq_u8(p8, chars1, cp), c62, val1);
        val1 = svsel_u8(svcmpeq_u8(p8, chars1, cs), c63, val1);
#endif
        svuint32_t pkd  = svbext_u32(svrevb_u32_x(p32,
            svreinterpret_u32_u8(val)), bm);
        svuint32_t pkd1 = svbext_u32(svrevb_u32_x(p32,
            svreinterpret_u32_u8(val1)), bm);
        svst1_u8(p_st, dst + g*3,
                 svtbl(svreinterpret_u8_u32(pkd),  un_idx));
        svst1_u8(p_st, dst + (g + ng)*3,
                 svtbl(svreinterpret_u8_u32(pkd1), un_idx));
    }
    /* remainder full chunks */
    for (; g < nf; g += ng) {
        svuint8_t chars = svld1_u8(p8, (const uint8_t*)(src + g*4));
#if defined(BASE64_SVE2_USE_TBL2_VL_GE512)
        svuint8_t val = svtbl2_u8(rt_full, chars);
#elif defined(BASE64_SVE2_USE_TBL2_VL256)
        svuint8_t r0  = svtbl2_u8(rt_lo, chars);
        svuint8_t r1  = svtbl2_u8(rt_hi, svsub_u8_x(p8, chars, c64));
        svuint8_t val = svsel_u8(svcmpge_u8(p8, chars, c64), r1, r0);
#else
        svuint8_t val = svadd_u8_x(p8,
            svtbl(t_hi, svlsr_n_u8_x(p8, chars, 4)),
            svand_u8_x(p8, chars, c0f));
        val = svsel_u8(svcmpeq_u8(p8, chars, cp), c62, val);
        val = svsel_u8(svcmpeq_u8(p8, chars, cs), c63, val);
#endif
        svuint32_t pkd = svbext_u32(svrevb_u32_x(p32,
            svreinterpret_u32_u8(val)), bm);
        svst1_u8(p_st, dst + g*3,
                 svtbl(svreinterpret_u8_u32(pkd), un_idx));
    }

    /* tail: partial chunk */
    if (g < n_groups) {
        size_t    tail  = n_groups - g;
        svbool_t  p_tail_ld = svwhilelt_b8((uint64_t)0, (uint64_t)(tail*4));
        svuint8_t chars = svld1_u8(p_tail_ld, (const uint8_t*)(src + g*4));
#if defined(BASE64_SVE2_USE_TBL2_VL_GE512)
        svuint8_t val = svtbl2_u8(rt_full, chars);
#elif defined(BASE64_SVE2_USE_TBL2_VL256)
        svuint8_t r0  = svtbl2_u8(rt_lo, chars);
        svuint8_t r1  = svtbl2_u8(rt_hi, svsub_u8_x(p8, chars, c64));
        svuint8_t val = svsel_u8(svcmpge_u8(p8, chars, c64), r1, r0);
#else
        svuint8_t val = svadd_u8_x(p8,
            svtbl(t_hi, svlsr_n_u8_x(p8, chars, 4)),
            svand_u8_x(p8, chars, c0f));
        val = svsel_u8(svcmpeq_u8(p8, chars, cp), c62, val);
        val = svsel_u8(svcmpeq_u8(p8, chars, cs), c63, val);
#endif
        svuint32_t pkd = svbext_u32(svrevb_u32_x(p32,
            svreinterpret_u32_u8(val)), bm);
        svuint8_t out  = svtbl(svreinterpret_u8_u32(pkd), un_idx);
        svst1_u8(svwhilelt_b8(0u, (uint32_t)(tail*3)),
                 dst + g*3, out);
        g = n_groups;
    }

    size_t out_len = n_groups * 3;
    if (src_len > 0 && src[src_len-1] == '=') out_len--;
    if (src_len > 1 && src[src_len-2] == '=') out_len--;
    return out_len;
}
