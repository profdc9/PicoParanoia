// cryptotool — base64 text-armor codec + small crypto helpers.
//
// Ported from ParanoiaBox cryptotool.cpp. The base64 codec is stream-oriented
// (read/write callbacks) so it composes with fileop's block reader/writer. The
// crypto helpers wrap the vendored primitives (BLAKE2s, and symmetric_memcrypt's
// AEAD cipher -- ChaCha20-Poly1305 as of this file; see cryptotool.h for why
// callers never need to know that). Two original helpers are intentionally
// omitted (see cryptotool.h): the unused ctblake2srehash and the sbrk-based
// heap_stack_distance.

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <BLAKE2s.h>
#include <ChaChaPoly.h>
#include "cryptotool.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------- base64 ----------------

static const char encoding_table[] = {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
            'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
            'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
            'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
            'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
            'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
            'w', 'x', 'y', 'z', '0', '1', '2', '3',
            '4', '5', '6', '7', '8', '9', '+', '/' };

static const uint8_t decoding_table[256] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3e, 0xFF, 0xFE, 0xFF, 0x3f,
            0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
            0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
            0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

int is_base64_char(char ch)
{
    return (decoding_table[(uint8_t)ch] != 0xFF);
}

int base64_encode(base64_readdata rd, void *vrd, base64_writedata wd, void *vwd)
{
    int len;
    do {
        int ch1, ch2 = 0, ch3 = 0;
        len = 0;
        ch1 = rd(vrd);
        if (ch1 < 0) ch1 = 0;
        else
        {
            len++;
            ch2 = rd(vrd);
            if (ch2 < 0) ch2 = 0;
            else
            {   len++;
                ch3 = rd(vrd);
                if (ch3 < 0) ch3 = 0;
                else len++;
            }
        }
        uint32_t triple = ((((uint32_t)ch1) << 0x10) +
                           (((uint32_t)ch2) << 0x08) +
                            ((uint32_t)ch3));
        if (len > 0)
        {
            wd(encoding_table[(triple >> 3 * 6) & 0x3F],vwd);
            wd(encoding_table[(triple >> 2 * 6) & 0x3F],vwd);
            wd(len > 1 ? encoding_table[(triple >> 1 * 6) & 0x3F] : '=',vwd);
            wd(len > 2 ? encoding_table[(triple >> 0 * 6) & 0x3F] : '=',vwd);
        }
    } while (len>2);
    return 1;
}

int base64_decode(base64_readdata rd, void *vrd, base64_writedata wd, void *vwd)
{
    int len;
    do {
        int ch1, ch2, ch3, ch4;
        len = 0;
        ch1 = rd(vrd);
        if ((ch1 >= 0) && (ch1 != '=')) len++;
        ch2 = rd(vrd);
        if ((ch2 >= 0) && (ch2 != '=')) len++;
        ch3 = rd(vrd);
        if ((ch3 >= 0) && (ch3 != '=')) len++;
        ch4 = rd(vrd);
        if ((ch4 >= 0) && (ch4 != '=')) len++;
        uint32_t triple = (((uint32_t)decoding_table[(uint8_t)ch1]) << 3 * 6)
            + (((uint32_t)decoding_table[(uint8_t)ch2]) << 2 * 6)
            + (((uint32_t)decoding_table[(uint8_t)ch3]) << 1 * 6)
            + (((uint32_t)decoding_table[(uint8_t)ch4]) << 0 * 6);
        if (len>1)
        {
            wd((triple >> 2 * 8) & 0xFF,vwd);
            if (len>2)
            {
                wd((triple >> 1 * 8) & 0xFF,vwd);
                if (len>3) wd((triple >> 0 * 8) & 0xFF,vwd);
            }
        }
    } while (len>3);
    return 1;
}

// ---------------- crypto helpers ----------------

int ctblake2s(void *out, size_t outlen, const void *in, size_t inlen, const void *key, size_t keylen)
{
    BLAKE2s blake2s;
    if (keylen > 0)
        blake2s.reset(key, keylen, outlen);
    else
        blake2s.reset(outlen);
    blake2s.update(in, inlen);
    blake2s.finalize(out, outlen);
    return 0;
}

/* PBKDF2-style stretch to a 32-byte key */
int key_derivation_function(void *hash, void *passphrase, size_t passphrase_len, void *salt, size_t salt_len)
{
    BLAKE2s blake2s;
    const uint8_t b[4] = { 0, 0, 0, 1 };
    uint8_t current_hash[KEYMANAGER_HASHLEN];

    blake2s.reset((uint8_t *)salt, salt_len, KEYMANAGER_HASHLEN);
    blake2s.update(b, sizeof(b));
    blake2s.update((uint8_t *)passphrase, passphrase_len);
    blake2s.finalize(hash, KEYMANAGER_HASHLEN);
    memcpy((void *)current_hash, (void *)hash, KEYMANAGER_HASHLEN);

    for (uint32_t n = 1; n < KEY_DERIVATION_HASHES; n++)
    {
        blake2s.reset((uint8_t *)current_hash, KEYMANAGER_HASHLEN);
        blake2s.update((uint8_t *)passphrase, passphrase_len);
        blake2s.finalize(current_hash, KEYMANAGER_HASHLEN);
        for (int i = 0; i < KEYMANAGER_HASHLEN; i++) ((uint8_t *)hash)[i] ^= current_hash[i];
    }
    return 0;
}

// The one place that names the actual AEAD cipher in use. Everything above
// this file (keymanager, fileenc) calls symmetric_memcrypt() and only ever
// sees SYMMETRIC_* sizes. Was GCM<AES256>; now ChaCha20-Poly1305 (RFC 8439),
// same 256-bit key / 96-bit IV / 128-bit tag, so no caller or on-disk/
// on-flash layout changed. FILEENC_EXPORT_VERSION was bumped alongside this
// swap so old AES-256-GCM files fail with a clear "wrong version" instead of
// a confusing tag mismatch.
//
// ChaChaPoly::ivSize() returns 8 (the original 64-bit-nonce ChaCha variant),
// NOT 12 -- unlike GCM<AES256>::ivSize(), which correctly reports its real
// wire IV length. So this function does NOT trust cipher.ivSize() the way
// the AES-GCM version did; SYMMETRIC_IV_WIRE_LEN below is the actual RFC 8439
// 96-bit nonce length, confirmed against the vendored library's own
// TestChaChaPoly.ino (which always passes 12 explicitly, never ivSize()).
// See third_party/crypto/PROVENANCE.md.
#define SYMMETRIC_IV_WIRE_LEN 12

int symmetric_memcrypt(bool encrypt, void *key, void *iv, void *tag, void *buffer, size_t inlen)
{
    ChaChaPoly cipher;
    cipher.setKey((const uint8_t *)key, cipher.keySize());
    cipher.setIV((const uint8_t *)iv, SYMMETRIC_IV_WIRE_LEN);
    if (encrypt)
    {
        cipher.encrypt((uint8_t *)buffer, (uint8_t *)buffer, inlen);
        cipher.computeTag((uint8_t *)tag, SYMMETRIC_TAGLEN);
        return 1;
    }
    cipher.decrypt((uint8_t *)buffer, (uint8_t *)buffer, inlen);
    return cipher.checkTag(tag, SYMMETRIC_TAGLEN);
}

#define poly 0x1021

uint32_t calc_crc16(uint8_t *addr, uint32_t num)
{
    int i;
    uint32_t crc = 0;
    for (; num > 0; num--)
    {
        crc = crc ^ (((uint32_t)*addr++) << 8);
        for (i = 0; i < 8; i++)
        {
            crc = crc << 1;
            if (crc & 0x10000)
                crc = (crc ^ poly) & 0xFFFF;
        }
    }
    return (crc);
}

#ifdef __cplusplus
}
#endif
