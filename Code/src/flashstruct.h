#ifndef _FLASHSTRUCT_H
#define _FLASHSTRUCT_H

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

// Serialize a set of variable-length blocks to/from a flash region. Same
// contract as the STM32 ParanoiaBox flashstruct, reimplemented for the RP2040
// (see flashstruct.c). flash_page is an XIP-mapped flash address; for
// writeflashstruct it must be flash-sector (4 KB) aligned. A NULL entry in
// blocks[] during a read skips that block (advancing by blocklen[]).

#ifdef __cplusplus
extern "C" {
#endif

int readflashstruct(void *flash_page, int num_blocks, void *blocks[], int blocklen[]);
int writeflashstruct(void *flash_page, int num_blocks, void *blocks[], int blocklen[]);
int flashstruct_selftest(void);

#ifdef __cplusplus
}
#endif

#endif  /* _FLASHSTRUCT_H */
