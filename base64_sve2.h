/*
 * base64_sve2.h — ARM SVE2 bdep/bext accelerated Base64 codec
 */
#ifndef BASE64_SVE2_H
#define BASE64_SVE2_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Encode ─────────────────────────────────────────────────────── */

/**
 * Base64 encode (SVE2 bdep accelerated)
 *
 * @param src     Input byte array
 * @param src_len Input byte count (any value)
 * @param dst     Output buffer
 * @param dst_cap Output buffer capacity in bytes
 * @return        Bytes written on success, 0 if dst_cap is too small
 */
size_t base64_encode_sve2(const uint8_t *src, size_t src_len,
                          char *dst, size_t dst_cap);

/* ── Decode ─────────────────────────────────────────────────────── */

/**
 * Base64 decode (SVE2 bext accelerated)
 *
 * @param src     Input Base64 string
 * @param src_len Input character count (excluding padding '=' characters)
 * @param dst     Output buffer
 * @param dst_cap Output buffer capacity in bytes
 * @return        Bytes decoded on success, 0 on error
 *                (invalid character or dst_cap too small)
 */
size_t base64_decode_sve2(const char *src, size_t src_len,
                          uint8_t *dst, size_t dst_cap);

#ifdef __cplusplus
}
#endif

#endif /* BASE64_SVE2_H */
