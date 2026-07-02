// SD/SDHC-over-SPI disk driver for FatFs — two independent buses.
//
// The SD protocol logic is ChaN's reference driver (mmc_stm32f1_spi.c from the
// STM32 ParanoiaBox) essentially unchanged. The only platform-specific parts —
// the byte exchange, chip select, and clock — are parameterized by the global
// `curdrv`, which every disk_* entry point sets. In the STM32 version the two
// cards shared one bus and `curdrv` only picked the CS line; here the two cards
// are on separate buses (PORTING.md §1.1/§2.3), so `curdrv` picks BOTH the SPI
// peripheral and its CS:
//
//   drive 0 = ciphertext -> hw spi1  (SCK GPIO10, MOSI GPIO11, MISO GPIO12, CS GPIO13)
//   drive 1 = plaintext  -> hw spi0  (SCK GPIO18, MOSI GPIO19, MISO GPIO20, CS GPIO21)

#include "ff.h"
#include "diskio.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

typedef struct { spi_inst_t *spi; uint8_t sck, mosi, miso, cs; } sd_bus_t;

static const sd_bus_t bus[FF_VOLUMES] = {
    { spi1, 10, 11, 12, 13 },   // drive 0: ciphertext
    { spi0, 18, 19, 20, 21 },   // drive 1: plaintext
};

static BYTE curdrv;                          // selects bus + CS for each exchange
#define SPIC (bus[curdrv].spi)
#define CS   (bus[curdrv].cs)

static DSTATUS Stat[FF_VOLUMES] = { STA_NOINIT, STA_NOINIT };
static BYTE    CardType[FF_VOLUMES];
static bool    BusInit[FF_VOLUMES];

// Card-type flags
#define CT_MMC   0x01
#define CT_SD1   0x02
#define CT_SD2   0x04
#define CT_SDC   (CT_SD1 | CT_SD2)
#define CT_BLOCK 0x08

// MMC/SD commands
#define CMD0   (0)          /* GO_IDLE_STATE */
#define CMD1   (1)          /* SEND_OP_COND (MMC) */
#define ACMD41 (0x80 + 41)  /* SEND_OP_COND (SDC) */
#define CMD8   (8)          /* SEND_IF_COND */
#define CMD9   (9)          /* SEND_CSD */
#define CMD12  (12)         /* STOP_TRANSMISSION */
#define ACMD13 (0x80 + 13)  /* SD_STATUS (SDC) */
#define CMD16  (16)         /* SET_BLOCKLEN */
#define CMD17  (17)         /* READ_SINGLE_BLOCK */
#define CMD18  (18)         /* READ_MULTIPLE_BLOCK */
#define ACMD23 (0x80 + 23)  /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24  (24)         /* WRITE_BLOCK */
#define CMD25  (25)         /* WRITE_MULTIPLE_BLOCK */
#define CMD55  (55)         /* APP_CMD */
#define CMD58  (58)         /* READ_OCR */

#ifndef SD_FAST_HZ
#define SD_FAST_HZ (12500 * 1000)   // post-init clock (SD SPI-mode default 12.5 MHz)
#endif
#define CS_HIGH()   gpio_put(CS, 1)
#define CS_LOW()    gpio_put(CS, 0)
#define FCLK_SLOW() spi_set_baudrate(SPIC, 400 * 1000)
#define FCLK_FAST() spi_set_baudrate(SPIC, SD_FAST_HZ)

// --- platform primitives (curdrv selects the bus) ---

static BYTE xchg_spi(BYTE dat) {
    uint8_t rx;
    spi_write_read_blocking(SPIC, &dat, &rx, 1);
    return rx;
}
static void rcvr_spi_multi(BYTE *buff, UINT btr) { spi_read_blocking(SPIC, 0xFF, buff, btr); }
static void xmit_spi_multi(const BYTE *buff, UINT btx) { spi_write_blocking(SPIC, buff, btx); }

static void bus_bringup(void) {
    if (!BusInit[curdrv]) {
        spi_init(SPIC, 400 * 1000);
        spi_set_format(SPIC, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);   // SD = SPI mode 0
        gpio_set_function(bus[curdrv].sck,  GPIO_FUNC_SPI);
        gpio_set_function(bus[curdrv].mosi, GPIO_FUNC_SPI);
        gpio_set_function(bus[curdrv].miso, GPIO_FUNC_SPI);
        gpio_pull_up(bus[curdrv].miso);
        gpio_init(CS);
        gpio_set_dir(CS, GPIO_OUT);
        gpio_put(CS, 1);
        BusInit[curdrv] = true;
    }
    CS_HIGH();
}

// --- protocol (ported verbatim; timeouts use the SDK clock) ---

static int wait_ready(UINT wt) {          // 1: ready, 0: timeout
    absolute_time_t dl = make_timeout_time_ms(wt);
    BYTE d;
    do { d = xchg_spi(0xFF); } while (d != 0xFF && !time_reached(dl));
    return d == 0xFF;
}

static void deselect(void) {
    CS_HIGH();
    xchg_spi(0xFF);   // dummy clock to release DO
}

static int select_card(void) {            // 1: ok, 0: timeout
    CS_LOW();
    xchg_spi(0xFF);
    if (wait_ready(500)) return 1;
    deselect();
    return 0;
}

static int rcvr_datablock(BYTE *buff, UINT btr) {
    BYTE token;
    absolute_time_t dl = make_timeout_time_ms(200);
    do { token = xchg_spi(0xFF); } while (token == 0xFF && !time_reached(dl));
    if (token != 0xFE) return 0;
    rcvr_spi_multi(buff, btr);
    xchg_spi(0xFF); xchg_spi(0xFF);       // discard CRC
    return 1;
}

static int xmit_datablock(const BYTE *buff, BYTE token) {
    if (!wait_ready(500)) return 0;
    xchg_spi(token);
    if (token != 0xFD) {                  // not StopTran
        xmit_spi_multi(buff, 512);
        xchg_spi(0xFF); xchg_spi(0xFF);   // dummy CRC
        if ((xchg_spi(0xFF) & 0x1F) != 0x05) return 0;
    }
    return 1;
}

static BYTE send_cmd(BYTE cmd, DWORD arg) {
    BYTE n, res;
    if (cmd & 0x80) {                     // ACMD<n>: CMD55 first
        cmd &= 0x7F;
        res = send_cmd(CMD55, 0);
        if (res > 1) return res;
    }
    if (cmd != CMD12) {                   // select except when stopping a read
        deselect();
        if (!select_card()) return 0xFF;
    }
    xchg_spi(0x40 | cmd);
    xchg_spi((BYTE)(arg >> 24));
    xchg_spi((BYTE)(arg >> 16));
    xchg_spi((BYTE)(arg >> 8));
    xchg_spi((BYTE)arg);
    n = 0x01;
    if (cmd == CMD0) n = 0x95;            // valid CRC for CMD0
    if (cmd == CMD8) n = 0x87;            // valid CRC for CMD8(0x1AA)
    xchg_spi(n);
    if (cmd == CMD12) xchg_spi(0xFF);     // discard following byte
    n = 10;
    do { res = xchg_spi(0xFF); } while ((res & 0x80) && --n);
    return res;
}

// --- FatFs disk interface ---

DSTATUS disk_initialize(BYTE drv) {
    if (drv >= FF_VOLUMES) return STA_NOINIT;
    curdrv = drv;
    bus_bringup();

    FCLK_SLOW();
    for (int n = 0; n < 10; n++) xchg_spi(0xFF);   // 80 dummy clocks, CS high

    BYTE ty = 0, ocr[4], n, cmd;
    if (send_cmd(CMD0, 0) == 1) {                  // enter idle/SPI state
        absolute_time_t dl = make_timeout_time_ms(1000);
        if (send_cmd(CMD8, 0x1AA) == 1) {          // SDv2?
            for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {              // 2.7-3.6V range?
                while (!time_reached(dl) && send_cmd(ACMD41, 1UL << 30)) ;
                if (!time_reached(dl) && send_cmd(CMD58, 0) == 0) {
                    for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
                    ty = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
                }
            }
        } else {                                    // SDv1 or MMC
            if (send_cmd(ACMD41, 0) <= 1) { ty = CT_SD1; cmd = ACMD41; }
            else                          { ty = CT_MMC; cmd = CMD1; }
            while (!time_reached(dl) && send_cmd(cmd, 0)) ;
            if (time_reached(dl) || send_cmd(CMD16, 512) != 0) ty = 0;
        }
    }
    CardType[drv] = ty;
    deselect();

    if (ty) { FCLK_FAST(); Stat[drv] &= ~STA_NOINIT; }
    else    { Stat[drv] = STA_NOINIT; }
    return Stat[drv];
}

DSTATUS disk_status(BYTE drv) {
    if (drv >= FF_VOLUMES) return STA_NOINIT;
    return Stat[drv];
}

DRESULT disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count) {
    if (drv >= FF_VOLUMES || !count) return RES_PARERR;
    curdrv = drv;
    if (Stat[drv] & STA_NOINIT) return RES_NOTRDY;

    DWORD sect = (DWORD)sector;
    if (!(CardType[drv] & CT_BLOCK)) sect *= 512;    // byte-addressing cards

    if (count == 1) {
        if (send_cmd(CMD17, sect) == 0 && rcvr_datablock(buff, 512)) count = 0;
    } else {
        if (send_cmd(CMD18, sect) == 0) {
            do { if (!rcvr_datablock(buff, 512)) break; buff += 512; } while (--count);
            send_cmd(CMD12, 0);
        }
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buff, LBA_t sector, UINT count) {
    if (drv >= FF_VOLUMES || !count) return RES_PARERR;
    curdrv = drv;
    if (Stat[drv] & STA_NOINIT) return RES_NOTRDY;
    if (Stat[drv] & STA_PROTECT) return RES_WRPRT;

    DWORD sect = (DWORD)sector;
    if (!(CardType[drv] & CT_BLOCK)) sect *= 512;

    if (count == 1) {
        if (send_cmd(CMD24, sect) == 0 && xmit_datablock(buff, 0xFE)) count = 0;
    } else {
        if (CardType[drv] & CT_SDC) send_cmd(ACMD23, count);
        if (send_cmd(CMD25, sect) == 0) {
            do { if (!xmit_datablock(buff, 0xFC)) break; buff += 512; } while (--count);
            if (!xmit_datablock(0, 0xFD)) count = 1;   // StopTran
        }
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE cmd, void *buff) {
    if (drv >= FF_VOLUMES) return RES_PARERR;
    curdrv = drv;
    if (Stat[drv] & STA_NOINIT) return RES_NOTRDY;

    DRESULT res = RES_ERROR;
    BYTE n, csd[16];
    DWORD csize;

    switch (cmd) {
    case CTRL_SYNC:
        if (select_card()) res = RES_OK;
        break;

    case GET_SECTOR_COUNT:
        if (send_cmd(CMD9, 0) == 0 && rcvr_datablock(csd, 16)) {
            if ((csd[0] >> 6) == 1) {   // SDC v2.00
                csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
                *(LBA_t *)buff = csize << 10;
            } else {                    // SDC v1.XX / MMC v3
                n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
                *(LBA_t *)buff = (LBA_t)csize << (n - 9);
            }
            res = RES_OK;
        }
        break;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 128;   // erase block size unknown; a safe default
        res = RES_OK;
        break;

    default:
        res = RES_PARERR;
    }
    deselect();
    return res;
}
