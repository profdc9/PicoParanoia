// pico_ntsc — composite scanout engine (step-2 first-light: static test pattern).
//
// Generates monochrome NTSC 240p (non-interlaced) composite via one PIO state
// machine + a self-reloading DMA loop over a precomputed full-frame buffer.
// No CPU or core1 involvement once started — the pattern is fully static.
//
// Timing (PORTING.md §1.5): sysclk 126 MHz, PIO sample clock 126/10 = 12.6 MHz.
// 800 samples/line => 63.49 us => 15750 Hz (B&W NTSC line rate). 262 lines/frame
// => 60.1 Hz. 800 = 50*16, so DMA 32-bit words align exactly to line boundaries.

#include "pico_ntsc.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "ntsc.pio.h"

// --- pins (PORTING.md §1.1) ---
#define NTSC_PIN_SYNC 16   // GPIO16, out bit 0
#define NTSC_PIN_LUMA 17   // GPIO17, out bit 1

// --- horizontal timing, in 12.6 MHz samples (1 sample ~= 79.4 ns) ---
#define LINE_SAMPLES 800
#define LINE_WORDS   (LINE_SAMPLES / 16)   // 2 bits/sample, 16 samples/word => 50
#define HSYNC        59                    // 4.7 us sync tip
#define BACKPORCH    57                    // 4.5 us
#define ACTIVE_START (HSYNC + BACKPORCH)    // 116
#define ACTIVE       640                   // 80 col * 8 px
#define HALF         (LINE_SAMPLES / 2)     // 400 (half-line, for vsync/eq)
#define EQ_PULSE     29                    // 2.3 us equalizing pulse
#define SERRATION    59                    // 4.7 us serration (high part of broad pulse)
#define VBROAD_LOW   (HALF - SERRATION)     // 341 (low part of vertical broad pulse)

// --- vertical structure, lines per frame = 262 ---
#define V_LINES      262
#define V_ACTIVE     200                   // 25 rows * 8 px (matches text geometry)

// One sample = 2 bits: bit0 = SYNC (0 = sync tip / low), bit1 = LUMA (1 = white).
static inline void put(uint32_t *line, int i, int sync, int luma) {
    uint32_t bits = (sync ? 1u : 0u) | (luma ? 2u : 0u);
    int w = i >> 4;
    int shift = (i & 15) * 2;
    line[w] = (line[w] & ~(3u << shift)) | (bits << shift);
}

// Blanking baseline: sync released (high) all line, luma black, with the HSYNC tip.
static void build_blank(uint32_t *l) {
    for (int i = 0; i < LINE_SAMPLES; i++) put(l, i, 1, 0);
    for (int i = 0; i < HSYNC; i++)        put(l, i, 0, 0);
}

// Equalizing pulses: two narrow sync pulses per line (start and mid-line).
static void build_eq(uint32_t *l) {
    for (int i = 0; i < LINE_SAMPLES; i++) put(l, i, 1, 0);
    for (int i = 0; i < EQ_PULSE; i++) { put(l, i, 0, 0); put(l, HALF + i, 0, 0); }
}

// Vertical sync: two broad (serrated) sync pulses per line.
static void build_vsync(uint32_t *l) {
    for (int i = 0; i < LINE_SAMPLES; i++) put(l, i, 1, 0);
    for (int i = 0; i < VBROAD_LOW; i++) { put(l, i, 0, 0); put(l, HALF + i, 0, 0); }
}

// Active line: 16px vertical bars with a 2px white left/right border.
static void build_bars(uint32_t *l) {
    build_blank(l);
    for (int c = 0; c < ACTIVE; c++) {
        int white = ((c / 16) & 1) == 0;
        if (c < 2 || c >= ACTIVE - 2) white = 1;   // side borders
        if (white) put(l, ACTIVE_START + c, 1, 1);
    }
}

// Solid white active line (top/bottom border of the active window).
static void build_solid(uint32_t *l) {
    build_blank(l);
    for (int c = 0; c < ACTIVE; c++) put(l, ACTIVE_START + c, 1, 1);
}

// --- frame buffer (SRAM/bss; copy-to-RAM build) ---
static uint32_t frame[V_LINES * LINE_WORDS];
static const uint32_t *const frame_src = frame;   // reload source for the DMA loop

static void build_frame(void) {
    uint32_t blank[LINE_WORDS], eq[LINE_WORDS], vsync[LINE_WORDS];
    uint32_t bars[LINE_WORDS],  solid[LINE_WORDS];
    build_blank(blank);
    build_eq(eq);
    build_vsync(vsync);
    build_bars(bars);
    build_solid(solid);

    int y = 0;
    #define EMIT(tpl, n) do { for (int k = 0; k < (n); k++) { \
        memcpy(&frame[y * LINE_WORDS], (tpl), sizeof(blank)); y++; } } while (0)

    EMIT(eq, 3);                 // pre-equalizing
    EMIT(vsync, 3);              // vertical sync
    EMIT(eq, 3);                 // post-equalizing
    EMIT(blank, 11);             // top blanking
    EMIT(solid, 1);              // active window: top border
    EMIT(bars, V_ACTIVE - 2);    //               body
    EMIT(solid, 1);              //               bottom border
    EMIT(blank, V_LINES - y);    // bottom blanking (fill to 262)
    #undef EMIT
}

static void start_dma_loop(PIO pio, uint sm) {
    int data_chan   = dma_claim_unused_channel(true);
    int reload_chan = dma_claim_unused_channel(true);

    // Data channel: frame -> PIO TX FIFO, paced by the TX DREQ; chains to reload.
    dma_channel_config dc = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&dc, reload_chan);
    dma_channel_configure(data_chan, &dc, &pio->txf[sm], frame,
                          count_of(frame), false);

    // Reload channel: rewrites the data channel's read address (trigger alias)
    // back to the frame start, restarting it with no gap — an endless loop.
    dma_channel_config rc = dma_channel_get_default_config(reload_chan);
    channel_config_set_transfer_data_size(&rc, DMA_SIZE_32);
    channel_config_set_read_increment(&rc, false);
    channel_config_set_write_increment(&rc, false);
    dma_channel_configure(reload_chan, &rc,
                          &dma_hw->ch[data_chan].al3_read_addr_trig,
                          &frame_src, 1, false);

    dma_channel_start(data_chan);
}

bool pico_ntsc_init_test_pattern(void) {
    // Lock the system clock to the value the line timing assumes.
    if (!set_sys_clock_khz(PICO_NTSC_SYS_CLOCK_KHZ, false)) return false;

    build_frame();

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &ntsc_program);

    pio_gpio_init(pio, NTSC_PIN_SYNC);
    pio_gpio_init(pio, NTSC_PIN_LUMA);
    pio_sm_set_consecutive_pindirs(pio, sm, NTSC_PIN_SYNC, 2, true);

    pio_sm_config c = ntsc_program_get_default_config(offset);
    sm_config_set_out_pins(&c, NTSC_PIN_SYNC, 2);
    sm_config_set_out_shift(&c, true, true, 32);   // shift right, autopull @32
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX); // deeper TX FIFO
    sm_config_set_clkdiv_int_frac(&c, 10, 0);      // 126 MHz / 10 = 12.6 MHz
    pio_sm_init(pio, sm, offset, &c);

    start_dma_loop(pio, sm);              // pre-fill FIFO before enabling
    pio_sm_set_enabled(pio, sm, true);
    return true;
}
