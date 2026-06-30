// pico_ntsc — composite scanout engine + text framebuffer.
//
// Monochrome NTSC 240p via one PIO SM (2 bits/sample: GPIO16 sync, GPIO17 luma)
// fed by a self-reloading DMA loop over a precomputed frame. Glyphs are blitted
// into the frame's active region; the cursor is XOR-rendered from a repeating
// timer. See pico_ntsc.h and PORTING.md §1.5.
//
// Timing: sysclk 126 MHz, sample clock 126/10 = 12.6 MHz. 800 samples/line =
// 63.49 us = 15750 Hz; 262 lines = 60.1 Hz. 800 = 50*16 -> DMA words align to
// line boundaries.

#include "pico_ntsc.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "ntsc.pio.h"

#define NTSC_PIN_SYNC 16   // out bit 0
#define NTSC_PIN_LUMA 17   // out bit 1

// --- horizontal timing, in 12.6 MHz samples ---
#define LINE_SAMPLES 800
#define LINE_WORDS   (LINE_SAMPLES / 16)   // 2 bits/sample, 16 samples/word -> 50
#define HSYNC        59
#define BACKPORCH    57
#define ACTIVE_START (HSYNC + BACKPORCH)    // 116
#define ACTIVE       640                   // 80*8 px
#define SERRATION    59                    // 4.7us: keeps one H-sync edge/line during vsync

// --- vertical structure ---
#define V_LINES      262
#define V_SYNC_LINES 3                     // progressive (240p) vertical sync, no equalizing
#define ACTIVE_LINE0 31                    // centers the 200-line active window in 262
#define CELL_H       8
#define CURSOR_BLINK_MS 300

// --- state ---
static uint32_t frame[V_LINES * LINE_WORDS];
static const uint32_t *const frame_src = frame;
static const uint8_t (*cur_font)[8] = pico_ntsc_font_unscii8;
static pico_ntsc_mode_t cur_mode = PICO_NTSC_MODE_80;
static int  cur_cols = 80;
static int  cur_row, cur_col;
static bool cur_visible, cur_shown;
static repeating_timer_t blink_timer;

// One 2-bit sample: bit0 = sync (0 = sync tip), bit1 = luma (1 = white).
static inline void put(uint32_t *l, int i, int sync, int luma) {
    uint32_t bits = (sync ? 1u : 0u) | (luma ? 2u : 0u);
    int w = i >> 4, shift = (i & 15) * 2;
    l[w] = (l[w] & ~(3u << shift)) | (bits << shift);
}
// Touch only the luma bit of a sample (active region; sync stays released).
static inline void luma_set(uint32_t *l, int i, int v) {
    int w = i >> 4, b = (i & 15) * 2 + 1;
    if (v) l[w] |= (1u << b); else l[w] &= ~(1u << b);
}
static inline void luma_xor(uint32_t *l, int i) {
    int w = i >> 4, b = (i & 15) * 2 + 1;
    l[w] ^= (1u << b);
}

// Normal line: HSYNC tip, then blanking (sync released, luma black).
static void build_blank(uint32_t *l) {
    for (int i = 0; i < LINE_SAMPLES; i++) put(l, i, 1, 0);
    for (int i = 0; i < HSYNC; i++)        put(l, i, 0, 0);
}
// Progressive (240p) vertical sync line: sync low almost the whole line, with a
// single serration at the end so the falling edge at the next line start stays
// the one-per-line horizontal reference. No interlace equalizing/half-line pulses.
static void build_vsync(uint32_t *l) {
    for (int i = 0; i < LINE_SAMPLES; i++)
        put(l, i, (i < LINE_SAMPLES - SERRATION) ? 0 : 1, 0);
}

static void build_frame(void) {
    uint32_t blank[LINE_WORDS], vsync[LINE_WORDS];
    build_blank(blank);
    build_vsync(vsync);

    int y = 0;
    #define EMIT(tpl, n) do { for (int k = 0; k < (n); k++) { \
        memcpy(&frame[y * LINE_WORDS], (tpl), sizeof(blank)); y++; } } while (0)
    EMIT(vsync, V_SYNC_LINES);   // vertical sync (lines 0..2)
    EMIT(blank, V_LINES - y);    // blanking; active rows are blitted in at ACTIVE_LINE0
    #undef EMIT
}

// --- blitting ---

// Apply one glyph row's 8 source pixels at samples starting at sample x0,
// honoring reverse video and the current horizontal scale (1 or 2 samples/px).
static void blit_row(uint32_t *line, int x0, uint8_t bits, int reverse, int scale) {
    for (int gc = 0; gc < 8; gc++) {
        int lum = ((bits & (0x80 >> gc)) ? 1 : 0) ^ (reverse ? 1 : 0);
        int s = x0 + gc * scale;
        luma_set(line, s, lum);
        if (scale == 2) luma_set(line, s + 1, lum);
    }
}

void pico_ntsc_put_cell(int row, int col, uint8_t cell) {
    if (row < 0 || row >= PICO_NTSC_ROWS || col < 0 || col >= cur_cols) return;
    int scale = (cur_mode == PICO_NTSC_MODE_40) ? 2 : 1;
    int x0 = ACTIVE_START + col * 8 * scale;
    uint8_t g = cell & 0x7F;
    int reverse = cell & 0x80;

    uint32_t save = save_and_disable_interrupts();
    if (cur_shown && row == cur_row && col == cur_col) {  // don't fight the cursor
        // erase cursor here; it will be re-shown by the timer from a clean cell
        cur_shown = false;
    }
    for (int gr = 0; gr < CELL_H; gr++) {
        uint32_t *line = &frame[(ACTIVE_LINE0 + row * CELL_H + gr) * LINE_WORDS];
        blit_row(line, x0, cur_font[g][gr], reverse, scale);
    }
    restore_interrupts(save);
}

void pico_ntsc_put_text(int row, int col, const char *s, bool reverse) {
    while (*s && col < cur_cols)
        pico_ntsc_put_cell(row, col++, (uint8_t)*s++ | (reverse ? 0x80 : 0));
}

void pico_ntsc_clear(void) {
    int scale = (cur_mode == PICO_NTSC_MODE_40) ? 2 : 1;
    (void)scale;
    uint32_t save = save_and_disable_interrupts();
    for (int r = 0; r < PICO_NTSC_ROWS; r++)
        for (int gr = 0; gr < CELL_H; gr++) {
            uint32_t *line = &frame[(ACTIVE_LINE0 + r * CELL_H + gr) * LINE_WORDS];
            for (int i = 0; i < ACTIVE; i++) luma_set(line, ACTIVE_START + i, 0);
        }
    cur_shown = false;
    restore_interrupts(save);
}

// --- cursor ---

// XOR the cursor cell's luma (toggles a reverse-video block).
static void cursor_toggle(void) {
    int scale = (cur_mode == PICO_NTSC_MODE_40) ? 2 : 1;
    int x0 = ACTIVE_START + cur_col * 8 * scale;
    for (int gr = 0; gr < CELL_H; gr++) {
        uint32_t *line = &frame[(ACTIVE_LINE0 + cur_row * CELL_H + gr) * LINE_WORDS];
        for (int gc = 0; gc < 8 * scale; gc++) luma_xor(line, x0 + gc);
    }
}

static bool blink_cb(repeating_timer_t *t) {
    (void)t;
    if (cur_visible) { cursor_toggle(); cur_shown = !cur_shown; }
    return true;
}

void pico_ntsc_set_cursor(int row, int col, bool visible) {
    uint32_t save = save_and_disable_interrupts();
    if (cur_shown) { cursor_toggle(); cur_shown = false; }   // hide at old position
    if (row >= 0) cur_row = row;
    if (col >= 0) cur_col = col;
    cur_visible = visible;
    restore_interrupts(save);
}

// --- setup ---

int  pico_ntsc_cols(void) { return cur_cols; }

void pico_ntsc_set_font(const uint8_t font[128][8]) {
    cur_font = (const uint8_t (*)[8])font;
}

void pico_ntsc_set_mode(pico_ntsc_mode_t mode) {
    pico_ntsc_clear();
    cur_mode = mode;
    cur_cols = (mode == PICO_NTSC_MODE_40) ? 40 : 80;
}

static void start_dma_loop(PIO pio, uint sm) {
    int data_chan   = dma_claim_unused_channel(true);
    int reload_chan = dma_claim_unused_channel(true);

    dma_channel_config dc = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&dc, reload_chan);
    dma_channel_configure(data_chan, &dc, &pio->txf[sm], frame, count_of(frame), false);

    dma_channel_config rc = dma_channel_get_default_config(reload_chan);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_32);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, false);
    dma_channel_configure(reload_chan, &rc,
                          &dma_hw->ch[data_chan].al3_read_addr_trig, &frame_src, 1, false);

    dma_channel_start(data_chan);
}

bool pico_ntsc_init(pico_ntsc_mode_t mode) {
    if (!set_sys_clock_khz(PICO_NTSC_SYS_CLOCK_KHZ, false)) return false;

    cur_mode = mode;
    cur_cols = (mode == PICO_NTSC_MODE_40) ? 40 : 80;
    cur_font = pico_ntsc_font_unscii8;
    cur_visible = false;
    cur_shown = false;
    cur_row = cur_col = 0;

    build_frame();

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &ntsc_program);
    pio_gpio_init(pio, NTSC_PIN_SYNC);
    pio_gpio_init(pio, NTSC_PIN_LUMA);
    pio_sm_set_consecutive_pindirs(pio, sm, NTSC_PIN_SYNC, 2, true);

    pio_sm_config c = ntsc_program_get_default_config(offset);
    sm_config_set_out_pins(&c, NTSC_PIN_SYNC, 2);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&c, 10, 0);
    pio_sm_init(pio, sm, offset, &c);

    start_dma_loop(pio, sm);
    pio_sm_set_enabled(pio, sm, true);

    add_repeating_timer_ms(-CURSOR_BLINK_MS, blink_cb, NULL, &blink_timer);
    return true;
}
