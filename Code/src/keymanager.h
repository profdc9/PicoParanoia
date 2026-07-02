#ifndef _KEYMANAGER_H
#define _KEYMANAGER_H

/*
 * Copyright (c) 2018 Daniel Marks

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

#include "cryptotool.h"

#ifdef __cplusplus
extern "C" {
#endif  

// 320 (4x the original 80): private keys never leave this device (no export
// path exists, by design), so the key store must hold every key -- private,
// public, and symmetric -- a user will ever need on it. Sized against the
// reserved 64 KB flash region (writeflashstruct's page-streaming rewrite
// means RAM is no longer the binding constraint -- see flashstruct.c): at
// 320, key_storage is ~30 KB, using 32 KB of flash (half the 60 KB budget
// after the self-test's reserved sector) and ~38% of free heap.
#define KEY_NUMBER 320
#define KEY_DESCRIPTION_LEN 30
// RP2040: base of the reserved persistent flash region (top 64 KB of the 2 MB
// part), XIP-mapped. Sector-aligned as writeflashstruct requires. The program
// image lives at the bottom of flash, far from here. (STM32 used 0x0801E000.)
#define KEYMANAGER_FLASH_STORAGE_ADDRESS 0x101F0000u

typedef enum
{
  KEY_TYPE_EMPTY = 0,
  KEY_TYPE_SYMMETRIC = 1,
  KEY_TYPE_ECDH_PRIVATE = 2,
  KEY_TYPE_ECDH_PUBLIC = 3
} key_type;

#define PAD_UP_TO_SYMMETRIC_BLOCKLEN(x) ((((x)+SYMMETRIC_BLOCKLEN-1)/SYMMETRIC_BLOCKLEN)*SYMMETRIC_BLOCKLEN)

#define KEY_EXPORT_ID 0xAA11
#define KEY_EXPORT_VERSION 0x1000

typedef struct _key_export_public
{
  uint16_t      id;
  uint16_t      vers;
  uint16_t      len;
  char          description[KEY_DESCRIPTION_LEN]; /* 30 bytes of description */
  uint8_t       public_key[KEYMANAGER_PRIVATEKEY_LEN];               
  uint16_t      crc16;
} key_export_public;

typedef struct _key_storage_symmetric
{
  uint8_t symmetric_key[KEYMANAGER_SYMMETRICKEY_LEN];             
} key_storage_symmetric;

typedef struct _key_storage_public
{
  uint8_t public_key[KEYMANAGER_PUBLICKEY_LEN];             
} key_storage_public;

typedef struct _key_storage_private
{
  uint8_t public_key[KEYMANAGER_PUBLICKEY_LEN];             
  uint8_t private_key[KEYMANAGER_PRIVATEKEY_LEN];             
} key_storage_private;

typedef union _key_storage_union
{
  key_storage_symmetric  sym;
  key_storage_private    priv;
  key_storage_public     pub;
} key_storage_union;

typedef struct _key_entry
{
  key_type entry_type;
	char description[KEY_DESCRIPTION_LEN]; /* 30 bytes of description */
  key_storage_union ksu;
} key_entry;

typedef union _key_entry_union
{
	key_entry kes[KEY_NUMBER];
	uint8_t   filler[PAD_UP_TO_SYMMETRIC_BLOCKLEN(sizeof(key_entry)*KEY_NUMBER)];    /* 64 bytes = 4 blocks */
} key_entry_union;

typedef struct _key_storage
{
	uint8_t          salt[KEYMANAGER_KEYBYTES];
  uint8_t          key_entry_iv[SYMMETRIC_IVLEN];
  uint8_t          key_entry_tag[SYMMETRIC_TAGLEN];
	key_entry_union  keu;
} key_storage;

void keymanager(void);
void keymanager_initialize(void);
void keymanager_display_key(int entno, key_entry *ke);
int keymanager_compute_secret(uint8_t *secret, int *secretlen);

extern key_entry current_key_private;
extern key_entry current_key_public;

#ifdef __cplusplus
}
#endif  

#endif  /* _KEYMANAGER_H */
