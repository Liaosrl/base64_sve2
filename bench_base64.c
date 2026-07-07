/* bench_base64.c -- SVE2 Base64 throughput benchmark */
#include "base64_sve2.h"
#include <arm_sve.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static const char B64[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t enc_ref(const uint8_t *src, size_t len, char *dst) {
    size_t out = 0;
    for (size_t i = 0; i + 2 < len; i += 3) {
        uint32_t v = ((uint32_t)src[i] << 16) |
                     ((uint32_t)src[i + 1] << 8) |
                      (uint32_t)src[i + 2];
        dst[out++] = B64[(v >> 18) & 0x3F];
        dst[out++] = B64[(v >> 12) & 0x3F];
        dst[out++] = B64[(v >> 6)  & 0x3F];
        dst[out++] = B64[ v        & 0x3F];
    }
    size_t rem = len % 3;
    if (rem) {
        size_t i = len - rem;
        uint32_t v = (rem == 1) ? ((uint32_t)src[i] << 16)
                     : (((uint32_t)src[i] << 16) |
                        ((uint32_t)src[i + 1] << 8));
        dst[out++] = B64[(v >> 18) & 0x3F];
        dst[out++] = B64[(v >> 12) & 0x3F];
        dst[out++] = (rem == 1) ? '=' : B64[(v >> 6) & 0x3F];
        dst[out++] = '=';
    }
    return out;
}

static uint8_t rev[256];
static void init_rev(void) {
    static int ready = 0;
    if (ready) return;
    memset(rev, 0xFF, 256);
    for (int i = 0; i < 64; i++) {
        rev[(uint8_t)B64[i]] = (uint8_t)i;
    }
    rev['='] = 0;
    ready = 1;
}

static size_t dec_ref(const char *src, size_t len, uint8_t *dst) {
    init_rev();
    size_t out = 0;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t s0 = rev[(uint8_t)src[i]];
        uint32_t s1 = rev[(uint8_t)src[i + 1]];
        uint32_t s2 = (src[i + 2] == '=') ? 0 : rev[(uint8_t)src[i + 2]];
        uint32_t s3 = (src[i + 3] == '=') ? 0 : rev[(uint8_t)src[i + 3]];
        uint32_t v  = (s0 << 18) | (s1 << 12) | (s2 << 6) | s3;
        dst[out++] = (uint8_t)(v >> 16);
        dst[out++] = (uint8_t)(v >> 8);
        dst[out++] = (uint8_t)v;
    }
    size_t pad = 0;
    if (len > 0 && src[len - 1] == '=') pad++;
    if (len > 1 && src[len - 2] == '=') pad++;
    return (len / 4) * 3 - pad;
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void fill_rnd(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)((i * 1103515245u + 12345u) & 0xFFu);
    }
}

static const char *unit(size_t n) {
    if (n >= 1000000000) return "GB";
    if (n >= 1000000)    return "MB";
    if (n >= 1000)       return "KB";
    return "B ";
}
static double scale(size_t n) {
    if (n >= 1000000000) return n / 1e9;
    if (n >= 1000000)    return n / 1e6;
    if (n >= 1000)       return n / 1e3;
    return (double)n;
}

int main(void) {
    size_t vl = svcntb();
    printf("=== Base64 SVE2 Throughput (VL=%zu bytes) ===\n\n", vl);

    size_t sizes[] = {
        1000, 10000, 50000, 100000, 200000, 500000,
        1000000, 2000000, 5000000, 10000000, 20000000,
        50000000, 100000000
    };
    int iterations[] = {
        500000, 50000, 10000, 5000, 2000, 1000,
        500, 250, 100, 50, 25,
        10, 5
    };
    int n_cases = 13;

    printf("  %6s %5s %9s %10s %10s %10s %10s %5s\n",
           "input", "out", "iters", "enc SVE2", "dec SVE2", "enc scal", "dec scal", "x");
    printf("  %s\n",
           "---------------------------------------------------------------------");

    double enc_min = 999, enc_max = 0, dec_min = 999, dec_max = 0;

    for (int i = 0; i < n_cases; i++) {
        size_t in_len   = sizes[i];
        size_t out_len  = ((in_len + 2) / 3) * 4;
        int    iters    = iterations[i];

        uint8_t *src = malloc(in_len + 64);
        char    *enc = malloc(out_len + 64);
        uint8_t *dec = malloc(in_len + 64);
        fill_rnd(src, in_len);

        /* encode */
        size_t enc_len;
        for (int w = 0; w < 5; w++) enc_len = base64_encode_sve2(src, in_len, enc, out_len + 64);
        double t0 = now();
        for (int j = 0; j < iters; j++) enc_len = base64_encode_sve2(src, in_len, enc, out_len + 64);
        double t1 = now();
        double enc_gbps = (in_len * (double)iters) / ((t1 - t0) * 1e9);

        /* decode */
        size_t dec_len;
        for (int w = 0; w < 5; w++) dec_len = base64_decode_sve2(enc, enc_len, dec, in_len + 64);
        double td0 = now();
        for (int j = 0; j < iters; j++) dec_len = base64_decode_sve2(enc, enc_len, dec, in_len + 64);
        double td1 = now();
        double dec_gbps = (in_len * (double)iters) / ((td1 - td0) * 1e9);

        /* scalar encode */
        for (int w = 0; w < 2; w++) enc_ref(src, in_len, enc);
        int sc_iters = iters / 5;
        double ts0 = now();
        for (int j = 0; j < sc_iters; j++) enc_ref(src, in_len, enc);
        double ts1 = now();
        double sc_enc = (in_len * (double)sc_iters) / ((ts1 - ts0) * 1e9);

        /* scalar decode */
        size_t sd_len;
        for (int w = 0; w < 2; w++) sd_len = dec_ref(enc, out_len, dec);
        double tds0 = now();
        for (int j = 0; j < sc_iters; j++) sd_len = dec_ref(enc, out_len, dec);
        double tds1 = now();
        double sc_dec = (in_len * (double)sc_iters) / ((tds1 - tds0) * 1e9);

        printf("  %5.0f %-2s %5.0f %-2s %9d %10.2f %10.2f %10.2f %10.2f %5.1fx\n",
               scale(in_len),  unit(in_len),
               scale(out_len), unit(out_len),
               iters, enc_gbps, dec_gbps, sc_enc, sc_dec,
               enc_gbps / sc_enc);

        if (in_len >= 50000) {
            if (enc_gbps < enc_min) enc_min = enc_gbps;
            if (enc_gbps > enc_max) enc_max = enc_gbps;
            if (dec_gbps < dec_min) dec_min = dec_gbps;
            if (dec_gbps > dec_max) dec_max = dec_gbps;
        }

        free(src); free(enc); free(dec);
    }

    printf("\n  Summary (50 KB - 100 MB):\n");
    printf("    encode:  %.1f - %.1f GB/s\n", enc_min, enc_max);
    printf("    decode:  %.1f - %.1f GB/s\n", dec_min, dec_max);
    return 0;
}
