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
//
// Defense in depth: every whitened block also folds in pico_ps2kbd's keystroke-
// timing accumulator (raw, unwhitened, human-typing-jitter based -- see
// pico_ps2kbd.h). It's blended in unconditionally, not just as a fallback: extra
// unpredictable material folded through BLAKE2s alongside the (already health-
// checked) ADC path cannot weaken the output, and it's a real fallback source of
// unpredictability if the analog noise circuit is ever degraded in a way the
// health checks don't catch. It contributes nothing (a fixed, known value) until
// someone has actually typed, which is honestly disclosed, not hidden.

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include <BLAKE2s.h>
#include <ff.h>
#include "consoleio.h"
#include "fileop.h"
#include "pico_ps2kbd.h"
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

// NIST SP 800-90B continuous health tests, run per channel on the raw low-byte
// stream. Cutoffs are for a false-positive rate alpha = 2^-20 and an assumed
// per-sample min-entropy of H = 2 bits (see the exact-cutoff table below). H=2
// is a deliberately conservative default: a healthy source spreading over ~120
// low-byte values has an expected APT count near 5, far under the 177 cutoff, so
// false alarms are negligible, yet the tests still catch a stuck value (RCT) and
// a two-value collapse (count ~256 > 177) that the range check alone misses.
// Tighten H toward the measured min-entropy once the offline SP 800-90B estimate
// is available (use the "Capture entropy to file" option to collect the samples).
//
//     H    RCT cutoff   APT cutoff   (W=512, alpha=2^-20)
//     1        21          310
//     2        11          177   <- current default
//     3         8          103
//     4         6           62
#ifndef RNG_RCT_CUTOFF
#define RNG_RCT_CUTOFF 11        // same low byte this many times in a row -> fail
#endif
#ifndef RNG_APT_WINDOW
#define RNG_APT_WINDOW 512       // non-binary APT window
#endif
#ifndef RNG_APT_CUTOFF
#define RNG_APT_CUTOFF 177       // most-common value >= this in a window -> fail
#endif

// Bytes captured per channel by "Capture entropy to file". SP 800-90B's
// estimators want >= 1,000,000 samples, so 1 MiB per channel clears the bar.
#ifndef RNG_CAPTURE_BYTES
#define RNG_CAPTURE_BYTES (1024u * 1024u)
#endif

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

// Per-channel state for the SP 800-90B continuous health tests.
typedef struct
{
  int16_t  rct_last;    // last sample value (-1 = none seen yet)
  uint16_t rct_run;     // current repetition run length
  uint8_t  apt_ref;     // reference value for the current APT window
  uint16_t apt_count;   // occurrences of apt_ref so far in the window
  uint16_t apt_pos;     // samples seen in the current window (0 = start)
} rng_health;

// Continuous state for the two channels, advanced by the whitened extractor so
// that APT windows accumulate across successive calls.
static rng_health health[2];

static void rng_health_reset(rng_health *hs)
{
  hs->rct_last  = -1;
  hs->rct_run   = 0;
  hs->apt_ref   = 0;
  hs->apt_count = 0;
  hs->apt_pos   = 0;
}

// Feed one raw low byte for a channel. Returns 0 (ok), 1 (RCT failed) or
// 2 (APT failed). Both tests run on every sample.
static int rng_health_update(rng_health *hs, uint8_t sample)
{
  // Repetition Count Test (SP 800-90B 4.4.1): flag a value repeating too often.
  if ((int16_t)sample == hs->rct_last)
  {
    if (++hs->rct_run >= RNG_RCT_CUTOFF) return 1;
  }
  else
  {
    hs->rct_last = (int16_t)sample;
    hs->rct_run  = 1;
  }

  // Adaptive Proportion Test (SP 800-90B 4.4.2): non-overlapping windows of
  // RNG_APT_WINDOW samples; flag if the window's first value recurs too often.
  if (hs->apt_pos == 0)
  {
    hs->apt_ref   = sample;
    hs->apt_count = 1;
    hs->apt_pos   = 1;
  }
  else
  {
    if (sample == hs->apt_ref) hs->apt_count++;
    if (++hs->apt_pos >= RNG_APT_WINDOW)
    {
      hs->apt_pos = 0;                          // start a fresh window next call
      if (hs->apt_count >= RNG_APT_CUTOFF) return 2;
    }
  }
  return 0;
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

// On-demand battery: for each channel run a full APT window through RCT + APT
// plus the peak-to-peak range check. Returns 1 if both channels pass, 0 if any
// test flags. Uses local test state so it doesn't perturb the continuous health
// windows advanced by the extractor. Surfaced in the UI so a bad circuit is
// caught before the user trusts the device with a key.
int random_circuit_check(void)
{
  for (int ch = 0; ch < 2; ch++)
  {
    rng_health hc;
    rng_health_reset(&hc);
    uint16_t mn = 0xFFFF, mx = 0;
    for (int i = 0; i < RNG_APT_WINDOW; i++)
    {
      uint16_t s = adc_sample(ch);
      if (s < mn) mn = s;
      if (s > mx) mx = s;
      if (rng_health_update(&hc, (uint8_t)s)) return 0;
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
  rng_health_reset(&health[0]);
  rng_health_reset(&health[1]);
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
// low-bytes (both channels), a per-block sequence number and timestamp for
// domain separation, and the current PS/2 keystroke-timing accumulator (defense
// in depth, see the file header), then take the 32-byte BLAKE2s digest. A
// per-block spread check guards against a stuck source; persistent failure
// panics rather than returning weak bits.
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

      uint32_t kb_acc = pico_ps2kbd_entropy_sample();
      uint32_t kb_cnt = pico_ps2kbd_entropy_count();
      h.update(&kb_acc, sizeof(kb_acc));
      h.update(&kb_cnt, sizeof(kb_cnt));

      uint16_t mn = 0xFFFF, mx = 0;
      int ch = 0;
      for (uint32_t i = 0; i < RNG_RAW_PER_BLOCK; i++)
      {
        int cur = ch;
        uint16_t s = adc_sample(cur);
        ch ^= 1;
        if (s < mn) mn = s;
        if (s > mx) mx = s;
        uint8_t lb = (uint8_t)s;
        // Continuous SP 800-90B health testing on the raw stream. A trip is
        // definitive (alpha = 2^-20), so we halt rather than retry.
        switch (rng_health_update(&health[cur], lb))
        {
          case 1: rng_panic("Repetition Count Test failed (value stuck)"); break;
          case 2: rng_panic("Adaptive Proportion Test failed (biased source)"); break;
        }
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
    // Uniformity check: for a flat distribution the bin counts scatter with
    // std dev ~sqrt(N/32), so the max-min spread grows like sqrt(N) while the
    // total count grows like N. Their ratio should therefore climb as sqrt(N)
    // the longer the test runs — a slowly rising number means uniform; a ratio
    // that stalls (spread growing like N) means a biased source.
    unsigned int hmax = 0, hmin = 0xFFFFFFFFu, total = 0;
    for (int n = 0; n < 32; n++)
    {
      unsigned int c = histogram[n];
      total += c;
      if (c > hmax) hmax = c;
      if (c < hmin) hmin = c;
    }
    unsigned int spread = hmax - hmin;

    unsigned long elapsed = (to_ms_since_boot(get_absolute_time()) - startime) / 1000;
    if (elapsed == 0) elapsed = 1;
    console_gotoxy(1, 20);
    console_puts("Total bits: ");
    console_printuint(bits);
    console_puts(" bit/s ");
    console_printuint(bits / elapsed);
    console_gotoxy(1, 21);
    console_puts("Spread (max-min): ");
    console_printuint(spread);
    console_gotoxy(1, 22);
    console_puts("Total/Spread (~sqrt N): ");
    console_printuint(spread ? total / spread : 0);
    console_gotoxy(1, 24);
    console_puts("Press SPACE to end");
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

// Stream RNG_CAPTURE_BYTES raw low bytes from a single channel (no alternation,
// no whitening, no health gating — one source per file is what SP 800-90B wants)
// to an SD file. Returns 1 on success.
static int capture_channel(int ch, const char *path)
{
  FIL f;
  if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
  {
    file_report_error("Could not create capture file");
    return 0;
  }
  uint8_t buf[512];
  FSIZE_t written = 0;
  while (written < RNG_CAPTURE_BYTES)
  {
    for (size_t i = 0; i < sizeof(buf); i++)
      buf[i] = (uint8_t)adc_sample(ch);
    UINT bw;
    if (f_write(&f, buf, sizeof(buf), &bw) != FR_OK || bw != sizeof(buf))
    {
      f_close(&f);
      file_report_error("Write error during capture");
      return 0;
    }
    written += bw;
    if ((written & (64u * 1024u - 1u)) == 0)
    {
      console_gotoxy(1, 6);
      console_puts("  ");
      console_printuint((unsigned)written);
      console_puts(" / ");
      console_printuint((unsigned)RNG_CAPTURE_BYTES);
      console_puts(" bytes ");
    }
  }
  f_close(&f);
  return 1;
}

// Capture both channels to <base>.CH0 and <base>.CH1 for offline min-entropy
// estimation. Lets the SP 800-90B assessment drive the RNG_* cutoffs above.
void randomness_capture_to_file(void)
{
  char base[256];
  if (!file_select_card("Select directory for entropy capture", base, sizeof(base) - 1, 1)) return;
  if (!file_enter_filename("Base filename (.CH0/.CH1 appended):", base, sizeof(base) - 1)) return;

  char p0[256], p1[256];
  strcpy_n(p0, base, sizeof(p0) - 1); strcat_n(p0, ".CH0", sizeof(p0) - 1);
  strcpy_n(p1, base, sizeof(p1) - 1); strcat_n(p1, ".CH1", sizeof(p1) - 1);

  console_clrscr();
  console_puts("Capturing raw low bytes (SP 800-90B input)\r\n");
  console_gotoxy(1, 4);
  console_puts("Channel 0 (GPIO26):");
  if (!capture_channel(0, p0)) return;
  console_gotoxy(1, 4);
  console_puts("Channel 1 (GPIO27):");
  if (!capture_channel(1, p1)) return;

  console_clrscr();
  console_puts("Capture complete:\r\n  ");
  console_puts(p0); console_printcrlf(); console_puts("  ");
  console_puts(p1); console_printcrlf();
  console_puts("\r\nAnalyze per channel on a host, e.g.:\r\n  ea_non_iid -a <file>\r\n");
  console_press_space();
}

#ifdef __cplusplus
}
#endif
