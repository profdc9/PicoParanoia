#ifndef _CRYPTOTOOL_H
#define _CRYPTOTOOL_H

// cryptotool — ported from ParanoiaBox cryptotool.cpp.
//
// The base64 text-armor codec plus the small crypto helpers layered on the
// vendored primitives (BLAKE2s, AES-256-GCM). Two helpers from the original are
// intentionally NOT ported: ctblake2srehash (declared but never defined/used
// upstream) and heap_stack_distance (a debug aid that relies on sbrk).

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
#define AES_KEYLEN                  32
#define AES_BLOCKLEN                16
#define AES_GCM_TAG_LENGTH          16
#define KEYMANAGER_SYMMETRICKEY_LEN AES_KEYLEN
#define KEYMANAGER_MAX_SECRET_LEN   KEYMANAGER_PUBLICKEY_LEN

/* BASE 64 encoding */

typedef int (*base64_readdata)(void *v);
typedef int (*base64_writedata)(int c, void *v);

int base64_encode(base64_readdata rd, void *vrd, base64_writedata wd, void *vwd);
int base64_decode(base64_readdata rd, void *vrd, base64_writedata wd, void *vwd);
int is_base64_char(char ch);

/* Crypto helpers over the vendored primitives */

int ctblake2s(void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen);
int aes256_gcm_memcrypt(bool encrypt, void *aes_key, void *aes_iv, void *tag, void *buffer, size_t inlen);
int key_derivation_function(void *hash, void *passphrase, size_t passphrase_len, void *salt, size_t salt_len);
uint32_t calc_crc16(uint8_t *addr, uint32_t num);

#ifdef __cplusplus
}
#endif

#endif  /* _CRYPTOTOOL_H */
