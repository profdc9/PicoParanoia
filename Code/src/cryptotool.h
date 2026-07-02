#ifndef _CRYPTOTOOL_H
#define _CRYPTOTOOL_H

// cryptotool — ported from ParanoiaBox cryptotool.cpp.
//
// The base64 text-armor codec plus the small crypto helpers layered on the
// vendored primitives (BLAKE2s, and an AEAD symmetric cipher -- ChaCha20-
// Poly1305 as of this file, see symmetric_memcrypt() in cryptotool.cpp; was
// AES-256-GCM). The SYMMETRIC_* sizes below name the *role* each value plays
// (key/IV/tag/padding), not the specific algorithm, so callers (keymanager,
// fileenc) never spell out which cipher is in use -- only cryptotool.cpp's
// implementation does. AES-256-GCM and ChaCha20-Poly1305 share these exact
// sizes (256-bit key, 96-bit IV, 128-bit tag), which is what made the swap
// possible without changing any caller or any on-disk/on-flash layout.
// Two helpers from the original are intentionally NOT ported: ctblake2srehash
// (declared but never defined/used upstream) and heap_stack_distance (a debug
// aid that relies on sbrk).

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sizes shared with keymanager/fileenc (canonical home, as in the original).
#define KEY_DERIVATION_HASHES       1000
#define KEY_MAX_PASSPHRASE_LENGTH   256
#define KEYMANAGER_HASHLEN          32
#define KEYMANAGER_KEYBYTES         32
#define KEYMANAGER_PUBLICKEY_LEN    32
#define KEYMANAGER_PRIVATEKEY_LEN   32
#define SYMMETRIC_KEYLEN            32   // cipher key length
#define SYMMETRIC_IVLEN             16   // IV/nonce storage (the wire IV is 12 of these bytes; sized generously)
#define SYMMETRIC_TAGLEN            16   // AEAD authentication tag length
#define SYMMETRIC_BLOCKLEN          16   // generic padding/alignment unit (key-table storage rounding)
#define KEYMANAGER_SYMMETRICKEY_LEN SYMMETRIC_KEYLEN
#define KEYMANAGER_MAX_SECRET_LEN   KEYMANAGER_PUBLICKEY_LEN

/* BASE 64 encoding */

typedef int (*base64_readdata)(void *v);
typedef int (*base64_writedata)(int c, void *v);

int base64_encode(base64_readdata rd, void *vrd, base64_writedata wd, void *vwd);
int base64_decode(base64_readdata rd, void *vrd, base64_writedata wd, void *vwd);
int is_base64_char(char ch);

/* Crypto helpers over the vendored primitives */

int ctblake2s(void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen);
int symmetric_memcrypt(bool encrypt, void *key, void *iv, void *tag, void *buffer, size_t inlen);
int key_derivation_function(void *hash, void *passphrase, size_t passphrase_len, void *salt, size_t salt_len);
uint32_t calc_crc16(uint8_t *addr, uint32_t num);

#ifdef __cplusplus
}
#endif

#endif  /* _CRYPTOTOOL_H */
