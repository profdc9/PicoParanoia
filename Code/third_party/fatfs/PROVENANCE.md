# Vendored: FatFs R0.16

- **Upstream:** http://elm-chan.org/fs/ff/ — ChaN's FatFs generic FAT filesystem.
- **Version:** R0.16 (2025-07-22), staged in `Code/archives/ff16.zip`.
- **License:** 1-clause BSD-style (ChaN); see `LICENSE.txt`.
- **Modifications:** none to the core. `ffconf.h` is configured (below); the
  bundled sample `diskio.c` is NOT vendored — we provide our own disk driver.

## Files

Vendored from `source/`: `ff.c`, `ff.h`, `ffunicode.c`, `ffsystem.c`,
`diskio.h`, `ffconf.h`. Not vendored: `diskio.c` (sample). The SD-over-SPI
implementation of `disk_initialize/status/read/write/ioctl` lives in
`Code/src/sd_spi.c` (PicoParanoia platform code — two independent SPI buses).

## ffconf.h configuration (matches the STM32 ParanoiaBox needs)

- `FF_VOLUMES = 2` — two cards: drive 0 = ciphertext (spi1), drive 1 = plaintext (spi0).
- `FF_FS_READONLY = 0`, `FF_USE_MKFS = 1` — read/write + format.
- `FF_USE_LFN = 1`, `FF_MAX_LFN = 255` — long file names (static work buffer).
- `FF_CODE_PAGE = 437` — US.
- `FF_FS_NORTC = 1` — no RTC; fixed timestamp.
- `FF_FS_REENTRANT = 0`, `FF_MIN_SS = FF_MAX_SS = 512`.
