// flashstruct — persist a set of variable-length blocks in flash (RP2040).
//
// Reimplementation of the ParanoiaBox flashstruct contract for the RP2040. The
// STM32 version poked the flash controller directly (half-word programming,
// 1 KB page erase at 0x0801E000). The RP2040 has XIP-mapped flash with 4 KB
// erase sectors and 256-byte program pages, so the mechanics differ, but the
// interface is unchanged: read/writeflashstruct serialize blocks contiguously.
//
// Reads are ordinary loads from the XIP window. Writes stage the serialized
// image in RAM, erase the covering 4 KB sectors, and program 256-byte pages.
//
// Why this is safe on this device: we run copy-to-RAM, so BOTH cores execute
// from SRAM and the video engine streams its framebuffer (also SRAM) over PIO+
// DMA — none of that touches flash. Erasing/programming therefore does not
// disturb the display or require pausing core1; we only need to keep core0 out
// of flash, which holds because this code and its callers are all in RAM. That
// is precisely the flash-bus-quiet property copy-to-RAM was chosen for.

#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/xip_ctrl.h"
#include "flashstruct.h"

// Reserved persistent region: top 64 KB of the (2 MB) Pico flash. The program
// image lives at the bottom of flash and is far smaller, so this never
// collides. The key store (keymanager) sits at the base of this region; the
// self-test uses a scratch sector near the top.
#define FLASHSTRUCT_TEST_OFFSET  (2u * 1024u * 1024u - FLASH_SECTOR_SIZE)  // last 4 KB sector

int readflashstruct(void *flash_page, int num_blocks, void *blocks[], int blocklen[])
{
  const uint8_t *src = (const uint8_t *)flash_page;
  for (int n = 0; n < num_blocks; n++)
  {
    if (blocks[n] != NULL)
      memcpy(blocks[n], src, (size_t)blocklen[n]);
    src += blocklen[n];
  }
  return 1;
}

int writeflashstruct(void *flash_page, int num_blocks, void *blocks[], int blocklen[])
{
  uint32_t offset = (uint32_t)((uintptr_t)flash_page - XIP_BASE);
  if (offset & (FLASH_SECTOR_SIZE - 1)) return 0;   // must be sector aligned

  size_t total = 0;
  for (int n = 0; n < num_blocks; n++) total += (size_t)blocklen[n];
  if (total == 0) return 0;

  size_t prog  = (total + FLASH_PAGE_SIZE   - 1) & ~((size_t)FLASH_PAGE_SIZE   - 1);
  size_t erase = (total + FLASH_SECTOR_SIZE - 1) & ~((size_t)FLASH_SECTOR_SIZE - 1);

  uint8_t *buf = (uint8_t *)malloc(prog);
  if (buf == NULL) return 0;
  memset(buf, 0xFF, prog);                          // 0xFF = erased flash (pad tail)
  size_t pos = 0;
  for (int n = 0; n < num_blocks; n++)
  {
    memcpy(buf + pos, blocks[n], (size_t)blocklen[n]);
    pos += (size_t)blocklen[n];
  }

  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(offset, erase);
  flash_range_program(offset, buf, prog);
  restore_interrupts(ints);

  // Invalidate the XIP cache so the verify (and later reads) see new contents.
  xip_ctrl_hw->flush = 1;
  while (!(xip_ctrl_hw->stat & XIP_STAT_FLUSH_READY_BITS))
    tight_loop_contents();

  int ok = (memcmp((const void *)flash_page, buf, total) == 0);
  memset(buf, 0, prog);                             // don't leave payload in RAM
  free(buf);
  return ok;
}

// On-hardware round-trip check of the flash primitive on a scratch sector, so
// the store can be trusted before keymanager depends on it. Returns 1 on pass.
int flashstruct_selftest(void)
{
  void *page = (void *)(uintptr_t)(XIP_BASE + FLASHSTRUCT_TEST_OFFSET);

  uint8_t a[100], b[7], a2[100], b2[7];
  for (int i = 0; i < (int)sizeof(a); i++) a[i] = (uint8_t)(i * 7 + 1);
  for (int i = 0; i < (int)sizeof(b); i++) b[i] = (uint8_t)(0xA5 ^ i);

  void *wblocks[2] = { a, b };
  int   wlens[2]   = { (int)sizeof(a), (int)sizeof(b) };
  if (!writeflashstruct(page, 2, wblocks, wlens)) return 0;

  memset(a2, 0, sizeof(a2));
  memset(b2, 0, sizeof(b2));
  void *rblocks[2] = { a2, b2 };
  int   rlens[2]   = { (int)sizeof(a2), (int)sizeof(b2) };
  if (!readflashstruct(page, 2, rblocks, rlens)) return 0;

  return (memcmp(a, a2, sizeof(a)) == 0) && (memcmp(b, b2, sizeof(b)) == 0);
}
