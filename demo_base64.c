/*
 * demo_base64.c -- SVE2 Base64 codec correctness test
 *
 * Build:
 *   gcc -march=armv8-a+sve2+sve2-bitperm -O2 \
 *       -o demo_base64 demo_base64.c base64_sve2.c
 */
#include "base64_sve2.h"
#include <stdio.h>
#include <string.h>
#include <arm_sve.h>

static const char B64[64] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
    'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
    'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
};

/* reference encode for verification */
static void b64enc3_ref(const uint8_t src[static 3], char dst[static 4]) {
    uint32_t v = ((uint32_t)src[0] << 16) |
                 ((uint32_t)src[1] << 8)  |
                  (uint32_t)src[2];
    dst[0] = B64[(v >> 18) & 0x3F]; dst[1] = B64[(v >> 12) & 0x3F];
    dst[2] = B64[(v >> 6)  & 0x3F]; dst[3] = B64[ v        & 0x3F];
}

/* ---- pipeline demo ---- */

static void demo_pipeline(void) {
    printf("======================================================\n");
    printf("  SVE2 pipeline: \"Man\" -> \"TWFu\"\n");
    printf("======================================================\n\n");

    uint8_t in[] = {'M','a','n'};
    char    out[8] = {0};
    size_t  n = base64_encode_sve2(in, 3, out, sizeof(out));

    printf("  ld1u8  ->  [M, a, n, ...]\n");
    printf("  tbl    ->  [0x6E, 0x61, 0x4D, 0] per u32\n");
    printf("  bdep   ->  sextet[3]=46='u'  sextet[2]=5='F'\n");
    printf("            sextet[1]=22='W'  sextet[0]=19='T'\n");
    printf("  revb   ->  [T, W, F, u] byte order\n");
    printf("  tbl+tbx->  B64 lookup\n");
    printf("\n  input: \"%.3s\" -> output: \"%.*s\" (expect \"TWFu\")\n\n",
           in, (int)n, out);
}

/* ---- correctness test ---- */

static int test_correctness(void) {
    struct {
        const uint8_t *in; size_t len; const char *expect;
    } tests[] = {
        {(const uint8_t*)"Man",   3, "TWFu"},
        {(const uint8_t*)"abc",   3, "YWJj"},
        {(const uint8_t*)"foo",   3, "Zm9v"},
        {(const uint8_t*)"bar",   3, "YmFy"},
        {(const uint8_t*)"xyz",   3, "eHl6"},
        {(const uint8_t*)"Hi!",   3, "SGkh"},
        {(const uint8_t*)"a",     1, "YQ=="},
        {(const uint8_t*)"ab",    2, "YWI="},
        {(const uint8_t*)"abcde", 5, "YWJjZGU="},
        {(const uint8_t*)"Hello", 5, "SGVsbG8="},
        {(const uint8_t*)"World!",6, "V29ybGQh"},
        {(const uint8_t*)"abcdefghijklmnopqrstuvwx", 24,
            "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4"},
    };
    int n = sizeof(tests) / sizeof(tests[0]);
    int pass = 0;

    printf("======================================================\n");
    printf("  Encode correctness\n");
    printf("======================================================\n\n");

    for (int i = 0; i < n; i++) {
        size_t elen = ((tests[i].len + 2) / 3) * 4;
        char sve2_out[256] = {0};
        size_t n_out = base64_encode_sve2(tests[i].in, tests[i].len,
                                          sve2_out, sizeof(sve2_out));

        /* build reference output */
        char ref_out[256] = {0};
        size_t ri = 0;
        for (size_t j = 0; j + 2 < tests[i].len; j += 3) {
            uint32_t v = ((uint32_t)tests[i].in[j] << 16) |
                         ((uint32_t)tests[i].in[j+1] << 8) |
                          (uint32_t)tests[i].in[j+2];
            ref_out[ri++]=B64[(v>>18)&0x3F]; ref_out[ri++]=B64[(v>>12)&0x3F];
            ref_out[ri++]=B64[(v>>6)&0x3F];  ref_out[ri++]=B64[v&0x3F];
        }
        size_t rem = tests[i].len % 3;
        if (rem) {
            size_t bi = tests[i].len - rem;
            uint32_t v = (rem == 1) ? ((uint32_t)tests[i].in[bi] << 16)
                         : (((uint32_t)tests[i].in[bi] << 16) |
                            ((uint32_t)tests[i].in[bi+1] << 8));
            ref_out[ri++] = B64[(v>>18)&0x3F]; ref_out[ri++] = B64[(v>>12)&0x3F];
            ref_out[ri++] = (rem == 1) ? '=' : B64[(v>>6)&0x3F];
            ref_out[ri++] = '=';
        }

        int ok = (n_out == elen) && (memcmp(sve2_out, ref_out, elen) == 0);
        printf("  [%s] %zuB -> \"%.*s\"  expect=\"%s\"\n",
               ok ? "OK" : "FAIL",
               tests[i].len, (int)elen, sve2_out, tests[i].expect);
        if (ok) pass++;
    }
    printf("\n  encode: %d/%d passed\n\n", pass, n);

    /* ---- decode test ---- */
    printf("======================================================\n");
    printf("  Decode correctness\n");
    printf("======================================================\n\n");

    const char *dec_in[] = {
        "TWFu","YWJj","Zm9v","YmFy","eHl6","SGkh",
        "YQ==","YWI=","YWJjZGU=","SGVsbG8=","V29ybGQh",
        "////","AAAA",
    };
    const uint8_t *dec_exp[] = {
        (const uint8_t*)"Man",(const uint8_t*)"abc",(const uint8_t*)"foo",
        (const uint8_t*)"bar",(const uint8_t*)"xyz",(const uint8_t*)"Hi!",
        (const uint8_t*)"a",(const uint8_t*)"ab",(const uint8_t*)"abcde",
        (const uint8_t*)"Hello",(const uint8_t*)"World!",
        (const uint8_t*)"\xff\xff\xff",(const uint8_t*)"\x00\x00\x00",
    };
    size_t dec_len[] = {3,3,3,3,3,3, 1,2,5,5,6, 3,3};
    int dn = 13, dpass = 0;

    for (int i = 0; i < dn; i++) {
        uint8_t out[256];
        size_t olen = base64_decode_sve2(dec_in[i], strlen(dec_in[i]),
                                         out, sizeof(out));
        int ok = (olen > 0) && (olen == dec_len[i]) &&
                 (memcmp(out, dec_exp[i], olen) == 0);
        printf("  [%s] \"%s\" -> ", ok ? "OK" : "FAIL", dec_in[i]);
        for (size_t j = 0; j < olen; j++) printf("%02x ", out[j]);
        printf("(%zuB)\n", olen);
        if (ok) dpass++;
    }
    printf("\n  decode: %d/%d passed\n\n", dpass, dn);

    /* ---- round-trip ---- */
    printf("======================================================\n");
    printf("  Round-trip\n");
    printf("======================================================\n\n");
    const char *rt = "SVE2 bext bdep Base64 codec test! 12345";
    size_t rt_len = strlen(rt);
    char   enc[256]; uint8_t dec[256];
    size_t elen = base64_encode_sve2((const uint8_t*)rt, rt_len,
                                     enc, sizeof(enc));
    size_t dlen = base64_decode_sve2(enc, elen, dec, sizeof(dec));
    int rt_ok = (dlen == rt_len) && (memcmp(dec, rt, rt_len) == 0);
    printf("  [%s] \"%s\" -> encode -> decode -> \"%.*s\"\n\n",
           rt_ok ? "OK" : "FAIL", rt, (int)dlen, dec);

    return (pass == n && dpass == dn && rt_ok) ? 0 : 1;
}

/* ---- main ---- */

int main(void) {
    printf("+======================================================+\n");
    printf("|   Base64 SVE2 Codec - Correctness Test               |\n");
    printf("|======================================================|\n");
    printf("|   VL: %3zu bytes (%zu u32 lanes)                       |\n",
           svcntb(), svcntw());
    printf("+======================================================+\n\n");

    demo_pipeline();
    return test_correctness();
}
