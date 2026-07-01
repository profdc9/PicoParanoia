#ifndef _CRYPTOTOOL_H
#define _CRYPTOTOOL_H

// cryptotool — ported from ParanoiaBox cryptotool.cpp.
//
// Currently only the base64 text-armor codec is ported (used by fileop's
// block read/write). The crypto helpers (ctblake2s, aes256_gcm_memcrypt,
// key_derivation_function, ...) are ported alongside fileenc, so this header
// grows to match the original then.

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BASE 64 encoding */

typedef int (*base64_readdata)(void *v);
typedef int (*base64_writedata)(int c, void *v);

int base64_encode(base64_readdata rd, void *vrd, base64_writedata wd, void *vwd);
int base64_decode(base64_readdata rd, void *vrd, base64_writedata wd, void *vwd);
int is_base64_char(char ch);

#ifdef __cplusplus
}
#endif

#endif  /* _CRYPTOTOOL_H */
