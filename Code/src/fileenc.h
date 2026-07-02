#ifndef _FILEENC_H
#define _FILEENC_H

/*
 * Copyright (c) 2020 Daniel Marks

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
 */

#include "keymanager.h"

#ifdef __cplusplus
extern "C" {
#endif  

#define FILEENC_EXPORT_ID 0xBBCC
// Bumped 0x1000 -> 0x1001: symmetric_memcrypt's AEAD cipher changed from
// AES-256-GCM to ChaCha20-Poly1305 (cryptotool.cpp). The on-disk layout is
// identical either way, so this correctly marks the new format for future
// reference -- but note it is NOT what rejects old files in practice.
// fileenc_decrypt_state() checks the header's AEAD tag before comparing
// fhp.vers, and decrypting an AES-GCM-tagged header with ChaCha20-Poly1305
// fails that tag check unconditionally (any key), so old files are actually
// rejected with "Header Tag is invalid" -- a stronger, cryptographic gate
// that fires first. The version check remains real protection for a future
// format change that keeps the same cipher (where the tag would still
// validate but field semantics changed).
#define FILEENC_EXPORT_VERSION 0x1001

#define FILEENC_FILENAME 256
#define FILEENC_FUTUREPROOF_LENGTH 512

#define FILEENC_DISPLAY_INCREMENT 20480

typedef struct _fileenc_header_payload
{
  uint16_t      id;
  uint16_t      entry_type;
  uint16_t      vers;
  uint16_t      len;
  uint16_t      totallen;
  uint64_t      file_length;
  uint8_t       salt2[KEYMANAGER_HASHLEN];
  uint8_t       iv2[SYMMETRIC_IVLEN];
  char          filename[FILEENC_FILENAME];
} fileenc_header_payload;

typedef union _fileenc_header_payload_union
{
  fileenc_header_payload fhp;
  uint8_t filler[FILEENC_FUTUREPROOF_LENGTH];
} fileenc_header_payload_union;

typedef struct _fileenc_total_header
{
  uint8_t                         salt1[KEYMANAGER_HASHLEN];
  uint8_t                         iv1[SYMMETRIC_IVLEN];
  uint8_t                         tag1[SYMMETRIC_TAGLEN];
  fileenc_header_payload_union    fhpu;
} fileenc_total_header;

void fileenc_encrypt(void);
void fileenc_decrypt(void);

#ifdef __cplusplus
}
#endif  

#endif  /* _FILEENC_H */
