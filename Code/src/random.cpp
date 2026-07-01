// random.cpp — hardware entropy for PicoParanoia (RP2040 rewrite).
//
// This replaces the STM32 random.cpp. The STM32 build fed a transistor noise
// source into the Arduino crypto library's ChaCha-based CSPRNG (RNG.h). The
// PicoParanoia design (PORTING.md) instead does DIRECT extraction: gather many
// raw ADC samples of the analog noise sources and compress them with BLAKE2s.
// No CSPRNG, no ChaCha, no NoiseSource object — the whole entropy path is these
// few functions, which keeps the trusted computing base small and auditable.
//
// Hardware: two independent analog noise sources feed the RP2040 ADC —
//   noise source 1 -> GPIO26 (ADC channel 0)
//   noise source 2 -> GPIO27 (ADC channel 1)
// The physical noise is only ~100 mV, so on the 3.3 V / 12-bit ADC (~0.8 mV per
// LSB) it lives in roughly the low ~7 bits, and realistically fewer once ADC
// filtering and any correlation are accounted for. We therefore (a) keep only
// the low byte of each conversion, (b) oversample VERY generously, and (c) rely
// on BLAKE2s as the randomness extractor. Getting the entropy accounting wrong
// here silently weakens every key, so the conservative knobs below are meant to
// be reviewed against real captured histograms (randomness_test / _show).

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include <BLAKE2s.h>
#include "consoleio.h"
#include "random.h"

// --- hardware map -----------------------------------------------------------
#define NOISE_GPIO0   26
#define NOISE_GPIO1   27
#define NOISE_CH0     0
#define NOISE_CH1     1
#define ADC_FULLSCALE 4095    // 12-bit ADC

// --- tunables (conservative starting points; revisit with hardware data) ----
//
// RNG_RAW_PER_BLOCK: raw ADC conversions (one low byte each) folded into BLAKE2s
// per 32-byte whitened output block. At a pessimistic ~1 bit of true entropy per
// low byte this still feeds 512 bits into a 256-bit digest (2x margin); at the
// more likely few-bits-per-sample it is a large safety factor. Deliberately
// generous per the "oversample very generously" design note.
#ifndef RNG_RAW_PER_BLOCK
#define RNG_RAW_PER_BLOCK 512
#endif

// RNG_MIN_SPREAD: minimum peak-to-peak spread (in ADC LSBs) a live noise source
// must show across a block. A healthy ~100 mV source swings ~120 LSB; a dead or
// rail-stuck source shows ~0. This threshold is intentionally low so ordinary
// noise passes easily while a genuinely stuck source is caught.
#ifndef RNG_MIN_SPREAD
#define RNG_MIN_SPREAD 12
#endif

// Bounded retries before we declare the source dead and refuse to continue.
#define RNG_STUCK_RETRIES 8

#ifdef __cplusplus
extern "C" {
#endif

// One 12-bit conversion from the selected noise channel.
static inline uint16_t adc_sample(int ch)
{
  adc_select_input(ch ? NOISE_CH1 : NOISE_CH0);
  return (uint16_t)(adc_read() & 0x0FFF);
}

// Refuse to emit guessable key material: if the entropy source is dead, halting
// loudly is far safer than returning predictable "randomness" on a device whose
// entire purpose is confidentiality. Reviewed decision — see PORTING.md.
static void rng_panic(const char *why)
{
  for (;;)
  {
    console_clrscr();
    console_highvideo();
    console_puts("ENTROPY SOURCE FAILURE\r\n");
    console_lowvideo();
    console_puts(why);
    console_puts("\r\nKeys/nonces cannot be generated safely.\r\nPower-cycle and check the noise circuit.");
    sleep_ms(1000);
  }
}

// Raw low bytes of successive conversions, alternating channels. For display and
// health testing only — NOT whitened, do not use directly as key material.
void randomness_get_raw_random_bits(uint8_t randomdata[], int bytes)
{
  int ch = 0;
  while (bytes > 0)
  {
    uint16_t s = adc_sample(ch);
    ch ^= 1;
    randomdata[--bytes] = (uint8_t)s;   // keep the noisy low bits
  }
}

// Sample a channel many times and confirm it actually moves. Returns 1 if both
// channels look live, 0 if either is flat/stuck. Exposed so the UI can surface
// a bad circuit before the user trusts the device with a key.
int random_circuit_check(void)
{
  for (int ch = 0; ch < 2; ch++)
  {
    uint16_t mn = 0xFFFF, mx = 0;
    for (int i = 0; i < 256; i++)
    {
      uint16_t s = adc_sample(ch);
      if (s < mn) mn = s;
      if (s > mx) mx = s;
    }
    if ((int)(mx - mn) < RNG_MIN_SPREAD) return 0;
  }
  return 1;
}

void random_initialize(void)
{
  adc_init();
  adc_gpio_init(NOISE_GPIO0);
  adc_gpio_init(NOISE_GPIO1);
  // Discard the first few conversions after enabling the analog pins.
  for (int i = 0; i < 16; i++) (void)adc_sample(i & 1);
}

// Warm-up hook kept for API compatibility with callers (e.g. keymanager). With
// direct extraction there is no CSPRNG pool to fill; we simply exercise the ADC.
void random_stir_in_entropy(void)
{
  uint8_t tmp[32];
  randomness_get_raw_random_bits(tmp, sizeof(tmp));
  memset(tmp, 0, sizeof(tmp));
}

// The extractor. For each 32-byte output block, hash RNG_RAW_PER_BLOCK raw ADC
// low-bytes (both channels) plus a per-block sequence number and timestamp for
// domain separation, then take the 32-byte BLAKE2s digest. A per-block spread
// check guards against a stuck source; persistent failure panics rather than
// returning weak bits.
void randomness_get_whitened_bits(uint8_t whitenedbytes[], size_t bytes)
{
  static uint32_t block_ctr = 0;
  size_t b = 0;

  while (b < bytes)
  {
    uint8_t digest[32];
    int tries = 0;

    for (;;)
    {
      BLAKE2s h;
      h.reset(32);

      uint32_t seq = block_ctr++;
      uint64_t t = time_us_64();
      h.update(&seq, sizeof(seq));
      h.update(&t, sizeof(t));

      uint16_t mn = 0xFFFF, mx = 0;
      int ch = 0;
      for (uint32_t i = 0; i < RNG_RAW_PER_BLOCK; i++)
      {
        uint16_t s = adc_sample(ch);
        ch ^= 1;
        if (s < mn) mn = s;
        if (s > mx) mx = s;
        uint8_t lb = (uint8_t)s;
        h.update(&lb, 1);
      }

      if ((int)(mx - mn) >= RNG_MIN_SPREAD)
      {
        h.finalize(digest, sizeof(digest));
        break;
      }
      if (++tries >= RNG_STUCK_RETRIES)
        rng_panic("Noise source flat (no variation)");
      // else retry: gather another block before giving up
    }

    size_t n = bytes - b;
    if (n > sizeof(digest)) n = sizeof(digest);
    memcpy(&whitenedbytes[b], digest, n);
    b += n;
    memset(digest, 0, sizeof(digest));   // don't leave key material on the stack
  }
}

// --- on-device diagnostics --------------------------------------------------

#define NUMBER_BLOCK 64

// Histogram of the low 5 bits of whitened output — should be flat. Exercises the
// full extractor path (ADC -> BLAKE2s) and reports throughput.
void randomness_test(void)
{
  unsigned int histogram[32];
  for (size_t n = 0; n < (sizeof(histogram) / sizeof(histogram[0])); n++) histogram[n] = 0;
  unsigned long startime = to_ms_since_boot(get_absolute_time());

  unsigned int bits = 0;
  for (;;)
  {
    int ch = console_inchar();
    if (ch == ' ') break;
    for (int n = 0; n < NUMBER_BLOCK; n++)
    {
      uint8_t randbits[32];
      randomness_get_whitened_bits(randbits, sizeof(randbits));
      for (size_t j = 0; j < sizeof(randbits); j++)
        histogram[randbits[j] & 0x1F]++;
      memset(randbits, 0, sizeof(randbits));
    }
    bits += (NUMBER_BLOCK * 32 * 8);
    console_clrscr();
    console_puts("Histogram (low 5 bits, want flat):\r\n\n");
    for (int n = 0; n < 16; n++)
    {
      console_gotoxy(1, n + 3);
      console_printint(n);
      console_puts(": ");
      console_printuint(histogram[n]);
      console_gotoxy(20, n + 3);
      console_printint(n + 16);
      console_puts(": ");
      console_printuint(histogram[n + 16]);
    }
    unsigned long elapsed = (to_ms_since_boot(get_absolute_time()) - startime) / 1000;
    if (elapsed == 0) elapsed = 1;
    console_gotoxy(1, 20);
    console_puts("Total bits: ");
    console_printuint(bits);
    console_puts(" bit/s ");
    console_printuint(bits / elapsed);
    console_puts("\r\nPress SPACE to end");
  }
}

// Raw ADC low bytes, refreshed once a second — lets you eyeball the actual noise
// on each channel (and spot a dead source directly).
void randomness_show(void)
{
  for (;;)
  {
    uint8_t samp[32];
    randomness_get_raw_random_bits(samp, sizeof(samp));
    console_clrscr();
    console_puts(random_circuit_check() ? "Circuit: OK\r\n\n" : "Circuit: SUSPECT\r\n\n");
    for (size_t i = 0; i < (sizeof(samp) / sizeof(samp[0])); i += 2)
    {
      console_printuint(samp[i]);
      console_puts(" ");
      console_printuint(samp[i + 1]);
      console_printcrlf();
    }
    sleep_ms(1000);
    int ch = console_inchar();
    if (ch == ' ') break;
  }
}

#ifdef __cplusplus
}
#endif
