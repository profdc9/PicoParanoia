# PicoParanoia — Porting Plan (STM32 ParanoiaBox → RP2040 Pico)

Status: **bring-up step 1 complete** (copy-to-RAM skeleton + vendored crypto
subset building; AES-256-GCM/BLAKE2s self-test ready to flash). Steps 2+ pending.

Porting the ParanoiaBox encryption terminal (`~/projects/ParanoiaBox/code`,
STM32F103CBT6 / stm32duino) to the Raspberry Pi Pico (RP2040), built on the
Pico SDK (vendored at `../pico-sdk`, v2.2.0).

Decision: keep the **Arduino Cryptography Library** (Rhys Weatherley). It is
pure software except for the RNG noise source, which we wire to the RP2040 ADC.

---

## 0. Design principles

This is an encryption device; its credibility rests on the firmware being
**auditable**. Two standing rules shape every decision in this document.

### A. Auditability first

- **Minimize the trusted computing base.** Every dependency is audit surface.
  Vendor and pin all third-party code (Pico SDK, Crypto lib, TinyUSB, FatFs)
  with recorded version + license + provenance; add nothing we can't justify.
- **Isolate the security-critical core** — crypto primitives, key management,
  RNG/entropy, flash key store, and the plaintext/ciphertext data path — into
  small, dependency-light modules reviewable on their own, separate from
  UI / video / USB / SD.
- **Document the trust boundary.** Be explicit about what touches secrets:
  keys & passphrases (`keymanager`, KDF), plaintext (`editor`, plaintext SD
  card, the video display, **and keyboard input — it carries the passphrase**).
  Non-secret: ciphertext, video sync/timing, the NTSC engine internals.
- **Zeroize secrets after use** — wipe key material, derived keys, passphrases,
  and plaintext buffers when done. The STM32 code is inconsistent here; the port
  is the chance to make this systematic.
- **Readable over clever.** Clear names, comments on *why*, modest functions, no
  undefined behavior; match the existing zlib-licensed style.
- **No hidden behavior.** Offline by design — no network, telemetry, or
  auto-update; no code path that isn't on the menu.
- **Constant-time where it matters.** Tag/secret comparisons go through the
  audited Crypto lib; flag any hand-written comparison over secret data.
- **Reproducible builds.** Pinned SDK submodule + toolchain + `copy_to_ram` so an
  auditor can rebuild the exact image and diff it. Document the build.

### B. Reusable, decoupled libraries

- The **NTSC/PAL video driver is a standalone, reusable library** — its own
  directory, its own CMake static-library target, a clean public header, and
  **zero dependency on PicoParanoia** (no `keymanager`/`consoleio`/etc.). It
  knows only about a framebuffer + timing config, so it drops into other
  projects unchanged.
- This *also serves auditability* (principle A): it lifts a large,
  non-security-critical component out of the crypto core's review surface.
- The `consoleio` shim sits **on top of** the video library, bridging it (and
  the keyboard) to the app — app-specific behavior stays out of the library.

---

## 1. Hardware differences (STM32 Blue Pill → RP2040 Pico)

### 1.1 Pin map (RP2040, confirmed 2026-06-30)

| Function | RP2040 GPIO | Notes |
|----------|-------------|-------|
| PS/2 DATA | GPIO4 | was `PB4` |
| PS/2 CLK | GPIO5 | was `PB5` |
| Ciphertext SD — SCK | GPIO10 | HW peripheral **spi1** |
| Ciphertext SD — MOSI | GPIO11 | |
| Ciphertext SD — MISO | GPIO12 | |
| Ciphertext SD — CS | GPIO13 | was shared bus + `PB8` CS |
| Plaintext SD — SCK | GPIO18 | HW peripheral **spi0** |
| Plaintext SD — MOSI | GPIO19 | |
| Plaintext SD — MISO | GPIO20 | |
| Plaintext SD — CS | GPIO21 | was shared bus + `PB9` CS |
| Video VSYNC | GPIO16 | NTSC/PAL |
| Video pixel clock | GPIO17 | NTSC/PAL |
| Noise transistor A | GPIO26 | ADC channel 0 |
| Noise transistor B | GPIO27 | ADC channel 1 |
| UART TX | GPIO0 | HW peripheral **uart0** |
| UART RX | GPIO1 | |
| UART CTS | GPIO2 | hardware flow control |
| UART RTS | GPIO3 | hardware flow control |

Exposed serial port (UART) — **not needed for the initial port, recorded for
later use.** GPIO0/1 are the RP2040 `uart0` function pins, with CTS/RTS flow
control on GPIO2/3.

### 1.2 Two SD cards on independent SPI buses (security-critical)

The STM32 version put **both** SD cards on a single SPI bus, distinguished
only by chip-select lines (`PB8` ciphertext, `PB9` plaintext). The Pico
version deliberately puts each card on its **own SPI controller** so that
neither card can electrically observe the other's traffic — preserving the
plaintext/ciphertext air-gap that is the whole point of the device.

- Ciphertext card → RP2040 **spi1** (GPIO10–13)
- Plaintext card → RP2040 **spi0** (GPIO18–21)

**Naming gotcha:** the schematic labels these nets "SPI2" (ciphertext) and
"SPI1" (plaintext), which are *off by one* from the RP2040 peripheral names
(`spi1` and `spi0` respectively). Code and comments must state which numbering
is meant. The FatFs disk-IO glue needs two independent SPI contexts, one bound
to each controller — not one bus with two CS lines.

### 1.3 Hardware RNG: dual simultaneous ADC → single muxed ADC

STM32 `random.cpp` drove `ADC1` and `ADC2` **simultaneously** (register-level
`SQR3`/`CR2`/`SR`/`DR` pokes) to sample two noise transistors at once. The
RP2040 has a **single** ADC multiplexed across channels. The two transistors
on ADC0 (GPIO26) and ADC1 (GPIO27) must be sampled in sequence (switch the
input mux between conversions, or use round-robin mode) and combined. The
output then feeds the Arduino Crypto library's `RNG` whitening.

#### 1.3.1 Low noise amplitude → only the lowest LSBs are random (critical)

The noise source is a **reverse-biased emitter–base junction**, and its noise
amplitude is only **~100 mV**. Against the RP2040's 12-bit ADC over a 3.3 V
reference (**~0.8 mV/LSB**), 100 mV spans only ~125 codes — and the *random*
content lives in just the **lowest few LSBs** of each sample. The upper bits
track the DC bias and slow drift, not entropy. So a raw ADC sample carries far
less real entropy than its bit-width suggests.

**Design mandate (do not skimp on entropy):**

- **Credit conservatively.** Assume only a small handful of bits — treat it as
  **≤1–2 bits of true min-entropy per ADC sample** (or less), never the full
  8/12 bits. Tell the `RNG`/`NoiseSource` credit accounting this low number.
- **Oversample *very* generously.** Gather many times more raw samples than the
  output needs and fold them all through the BLAKE2s/ChaCha whitening pool. For
  any key, nonce, IV, salt, or ECDH scalar, target a large safety margin
  (**≫10×** the credited bits required) so the noise source is **never** the
  limiting factor. When in doubt, gather more.
- **Block until satisfied.** Before generating key material, force a generous
  entropy stir-in and wait until the pool has absorbed well beyond the nominal
  requirement — never produce a key/nonce from a thin pool.
- **Debias / combine.** Prefer the low bits; consider von Neumann debiasing and
  XOR-combining the two independent channels (GPIO26/GPIO27) before crediting.
- **Health check.** Implement a real stuck-source detector (the STM32
  `random_circuit_check()` was an empty stub): if the LSBs go constant the
  junction/biasing has failed, and oversampling a dead source yields nothing —
  fail loudly rather than emit predictable keys.

This is a security-critical, auditable property (§0.A): generosity here is
deliberate and must be obvious in the code and its comments.

#### 1.3.2 RNG construction: direct BLAKE2s extraction (decided)

The conditioner is a **direct hash extractor**: sample the noise generously,
fold it through **BLAKE2s**, and take the digest as the random output — no
long-lived DRBG state. Chosen over an extract-then-DRBG hybrid because our
random demand is small and intermittent (keys, nonces, IVs, salts, ECDH
scalars), and a stateless "hash a big pile of fresh noise" construction is the
easiest thing for an auditor to trust (§0.A).

- **Memory cost is negligible — no need to buffer the oversampled noise.**
  BLAKE2s is incremental, so samples are `update()`-ed into the hash state as
  they're collected and immediately discarded. The only standing memory is the
  BLAKE2s context (~100–200 B) plus a tiny per-sample working buffer — *not* the
  thousands of raw samples. (Even copy-to-RAM's 264 KB would swallow a multi-KB
  buffer easily; we simply don't need one.)
- **One 256-bit extraction covers every secret we generate.** BLAKE2s' 32-byte
  digest ≥ AES-256 key, ECDH25519 scalar, and any nonce/IV. If something ever
  needs >256 bits, run **independent** extractions over fresh noise (or a keyed
  counter expansion), never stretch one digest.
- **Per call:** reset BLAKE2s → stream in ≫(output bits) of credited noise
  (debiased, two channels XOR-combined, §1.3.1), running the stuck-source check
  as samples arrive → `finalize()` → output. Block until the credited input
  margin is met.
- Reminder: extraction makes a **dead source look random** (§1.3.1), so the
  health check during sampling is what actually guarantees the result, not the
  hash.

### 1.4 Internal flash key storage: STM32 flash controller → RP2040 QSPI

STM32 `flashstruct.cpp` programmed the on-die flash via `FLASH_BASE->KEYR`
unlock + page erase, and `keymanager.h` hardcodes `0x0801E000` (STM32 memory
map). The RP2040 has **no internal flash** — code/data live in external QSPI
flash. Key storage must move to the SDK's `hardware/flash.h`
(`flash_range_erase` / `flash_range_program`, 4 KB sectors). The hardcoded
address and the 16-bit half-word write granularity assumptions go away.

Interaction with copy-to-RAM (§1.9): because the firmware runs entirely from
RAM, the usual "flash writes must execute from RAM with IRQs off" hazard is
largely handled by construction — no code is being fetched from the flash we're
erasing. Reserve a dedicated key region at the **top of flash** (outside the
copied program image) for the encrypted key store. Reading keys back is an
XIP-mapped read of that region; that traffic is acceptable because only
**ciphertext** key blobs ever cross the bus (see §1.9).

### 1.5 NTSC/PAL video generation

STM32 used the `TNTSChar` library, which generated composite video by abusing
an SPI peripheral + timers (libmaple). On RP2040 both SPI controllers are spoken
for by the SD cards, so composite video is generated with **PIO + DMA**.
Architecture settled 2026-06-30; this is the largest single new component, but
fully specified below.

**Output DAC — 2-resistor summing into 75 Ω composite.**
GPIO16 (sync) via **560 Ω** and GPIO17 (luma) via **240 Ω** sum into the 75 Ω-
terminated TV input. Measured/computed levels:

| State | GPIO16 | GPIO17 | Composite |
|-------|--------|--------|-----------|
| Sync tip | 0 V | 0 V | 0 V |
| Black/blanking | 3.3 V | 0 V | ≈0.31 V |
| White | 3.3 V | 3.3 V | ≈1.02 V |

Standard 0 / 0.3 / 1.0 V composite. **Monochrome** — no colorburst, so the pixel
clock is free of the 3.579545 MHz subcarrier constraint.

**Display model.**
- **Text cells only**, 240p **progressive** (non-interlaced, one field repeated).
  NTSC first; PAL later = different line/field counts + sync templates, same
  machinery (keep all timing in one table).
- **25 rows × 8 px = 200 active lines**, identical in both column modes — so the
  GPIO16 sync waveform is **mode-independent in real time**.
- **40-column (320 active px) and 80-column (640 active px)** both supported,
  switchable like `TNTSChar`.
- **1bpp framebuffer** rendered from a font ROM. Glyphs are drawn into the
  framebuffer **only on text change** (in `console_putch`/scroll), never per
  scanline. Sizes: 40-col 8 KB, 80-col 16 KB (×2 if double-buffered) — trivial
  under copy-to-RAM (§1.8).

**PIO scanout engine.**
- **Two SMs in one PIO block sharing one `clkdiv`**, started together
  (`pio_enable_sm_mask_in_sync`): one drives GPIO16 (sync), one drives GPIO17
  (luma), bit-aligned.
- SMs run **free-running for the whole field**; DMA is **ping-pong/chained** so
  the FIFOs never underrun. (Underrun = loss of lockstep = torn sync — this is
  *the* failure mode to design out, hence no per-line SM restarts.) The per-line
  IRQ only swaps buffer pointers.
- The **sync SM** streams a small set of precomputed line templates selected per
  line by a DMA descriptor list: normal-active, blank, pre-equalizing,
  broad/serration, post-equalizing (NTSC field structure).
- The **luma SM** streams a shared "black" buffer around the active framebuffer
  row each line, so porch/blanking is handled by DMA sequencing and the
  framebuffer stays pure picture (no per-line copy).

**40 vs 80 columns.** Keep the PIO clock **fixed at the 80-col pixel rate**
(~12.2 MHz); 80-col = 1 framebuffer bit/clock, 40-col = each bit held 2 clocks
(double-width pixels). Sync templates and clock are untouched between modes —
only the luma SM packing + framebuffer geometry change. (Alternative: halve the
shared `clkdiv` + mode-specific sync templates. Either way a mode switch
re-inits the engine at a field boundary, which is rare.)

**Pixel clock.** 80-col: 640 px / ~52.6 µs ≈ **12.2 MHz**; 40-col ≈ 6.1 MHz.
Choose sysclk + active-pixel count for an integer / low-jitter PIO divider
(monochrome gives freedom here).

**Scrolling.** A text scroll moves pixels, not just chars, but the cost is
negligible — an 80-col scroll `memmove`s ~15 KB (framebuffer minus one char
row), << one line time. Do it during **vertical blanking** (~3.9 ms available)
to avoid tearing, or render to the back framebuffer and swap. Zero-copy upgrade
if ever needed: a per-line pointer table (ring framebuffer) so a scroll rotates
pointers instead of moving pixels.

**Core split.** Run the video engine (SMs + DMA IRQs) on **core1**; app / UI /
USB / SD / crypto on **core0**, to isolate sync timing from system jitter.

Remaining choices are implementation-level only: exact sysclk/divider, the
scroll-tearing mitigation (vblank memmove vs double-buffer), and the 40-col
mechanism (double-width vs `clkdiv` change).

### 1.6 Keyboard input — PS/2 *or* USB host (new requirement)

The Pico version must accept **either a PS/2 keyboard or a USB keyboard**, a
capability the STM32 version did not have.

- **PS/2:** was the `PS2Keyboard` lib bit-banging `PB4`/`PB5`; same protocol,
  new pins (GPIO4 data, GPIO5 clk). RP2040 PIO program or IRQ bit-bang. Low risk.
- **USB host:** the RP2040 acts as a **USB host** and reads a USB HID boot
  keyboard via TinyUSB host mode. Both sources feed a single key event queue
  that `consoleio` drains, so the rest of the system is source-agnostic.

### 1.7 USB role: device (debug) → host (keyboard) — and what it costs stdio

The RP2040 has **one native USB controller**, which is *either* device *or*
host — not both at once on that port. This drives a deliberate two-phase plan:

- **Bring-up phase:** native USB in **device** mode = USB-CDC serial, used as
  the debug `stdio` console (printf, crypto self-tests, etc.).
- **Target phase:** native USB switches to **host** mode to drive the USB
  keyboard. At that point **USB-CDC stdio is gone** — any persistent debug
  console must move to **`uart0` (GPIO0/1)**, or be compiled out entirely for
  the shipping build.

Implication: treat USB-CDC stdio as **temporary scaffolding**. Keep all debug
output behind a thin logging shim with a build-time backend switch
(USB-CDC ↔ `uart0` ↔ none) so flipping the USB role doesn't ripple through the
code. (A second USB port via Pico-PIO-USB — host on PIO pins while native stays
device — is a possible future option but is **not** assumed here; the board
allocates no pins for it.)

**USB is off by default (least privilege / auditability, §0.A).** Some users
will not trust the USB stack on an encryption device, so USB must be **entirely
compile-time optional and inert unless explicitly enabled**:

- `PICOPARANOIA_ENABLE_USB_HOST` (default **OFF**) — compiles out the TinyUSB
  host + HID-keyboard driver. With it off, the keyboard is PS/2-only and no USB
  code is linked at all.
- `PICOPARANOIA_ENABLE_USB_STDIO` (default **OFF**) — USB-CDC debug console is
  opt-in even during bring-up; a developer turns it on deliberately. Default
  debug target is `uart0` (or none).
- A build with both off links **no USB code**, and the native USB peripheral is
  left uninitialized/inert. Each flag is independent so "USB keyboard, no debug
  CDC" and "CDC debug, PS/2 keyboard" are both valid configs (and the two CDC vs
  host modes remain mutually exclusive per the controller limit above).

This keeps the default firmware's attack/audit surface minimal: no USB unless
the operator chose it.

### 1.8 Execution entirely from RAM (copy-to-RAM) — flash-bus hardening

**Requirement:** the whole firmware image is copied from flash into RAM at
startup and executes from **SRAM**, so that no instruction fetches go out over
the external QSPI flash lines at runtime. Default RP2040 operation is XIP
(execute-in-place), where every cache-missed fetch is visible on the flash bus;
an attacker probing those lines could infer secret-dependent control flow and
data accesses. Running from RAM removes that side channel.

- **Mechanism:** the Pico SDK provides this out of the box —
  `pico_set_binary_type(picoparanoia copy_to_ram)` selects the copy-to-RAM
  linker layout; boot stage 2 copies the image to SRAM and jumps to it. No
  custom bootloader needed.
- **RAM budget (hard constraint):** everything — code, rodata, data, bss,
  stack, heap, **plus** the text-cell video buffer, FatFs/file buffers, USB-host
  buffers, and crypto working state — must fit in the RP2040's **264 KB SRAM**.
  This firmware is small, but watch the file-encryption buffers and keep an eye
  on the map file as modules land. Treat RAM headroom as a tracked budget.
- **Flash stays available for the encrypted key store only.** After the boot
  copy, the flash is otherwise idle. The only runtime bus traffic to flash is
  reading/writing the **encrypted** key region (§1.4) — and because those blobs
  are ciphertext, observing them on the bus leaks nothing useful.
- **Boot copy is not a secret-bearing operation:** the one-time bulk copy of the
  (non-secret) program image happens before any key is decrypted, so the
  unavoidable flash traffic at boot carries no secrets.
- Algorithmic constant-time behavior (execution-*time* side channels) is a
  separate concern, handled in the crypto library, not by this RAM decision.

> Possible hardening to evaluate later: actively quiesce/disable the XIP cache
> and flash between key accesses so the bus is fully idle except during explicit
> encrypted-key reads/writes.

### 1.9 Toolchain / framework

stm32duino Arduino core → bare-metal Pico SDK (CMake, `arm-none-eabi`). No
`Arduino.h`, `Serial`, libmaple, or stm32duino `SPI`/`RNG` glue. `printf`
routes through the logging shim (§1.7). Crypto lib must be compiled as plain
C++ against the SDK rather than the Arduino build. USB stack: **TinyUSB**
(bundled with the SDK) in device mode early, host mode later. Binary type:
**copy-to-RAM** (§1.8).

---

## 2. Module port strategy

Legend: **Reuse** = compiles essentially unchanged · **Shim** = keep the
module's API/logic, swap the platform calls underneath · **Rewrite** = replace
the implementation · **New** = no STM32 counterpart.

| Module (old) | Disposition | Work |
|--------------|-------------|------|
| `mini-printf.*` | **Reuse** | Pure C string formatting. |
| `debugmsg.*` | **Shim** | Route through the logging shim (§1.7): `uart0` by default, USB-CDC only if `PICOPARANOIA_ENABLE_USB_STDIO` is set, or none. Not `Serial`. |
| `cryptotool.*` | **Reuse** | AES/GCM/BLAKE2s via Crypto lib, base64, CRC16, KDF — all software. Only `heap_stack_distance()` (uses `sbrk`/frame address) needs an SDK-friendly version or removal. |
| Crypto library | **Reuse (recompile, pinned subset)** | Vendor `libraries/Crypto/` at commit `37a76b8` (§2.3) — only the GCM/AES256/CTR/BLAKE2s/Curve25519 subset. Compile as plain C++ against the SDK. **Drop `RNGClass`/`ChaCha`/`NoiseSource`/Arduino glue entirely** — direct BLAKE2s extraction (§1.3.2) replaces the DRBG, so that Arduino-coupled code never enters the build. |
| `editor.c` | **Reuse** | Talks only to the `consoleio` API; works once console is up. |
| `keymanager.*` | **Shim** | Logic is portable. Replace the flash-storage backing (see `flashstruct`) and the hardcoded `0x0801E000` address with an RP2040 QSPI region. |
| `fileenc.*` | **Reuse** | Pure logic over `keymanager` + `fileop` + `cryptotool`. |
| `fileop.*` | **Shim** | FatFs calls stay; the disk-IO layer underneath becomes two independent SPI/SD contexts (see below). `fs0`=ciphertext, `fs1`=plaintext. |
| FatFs | **Update + new glue** | Move to **R0.16** (§2.3), replacing the old STM32-glued `ElmChanFatFs`. Portable `source/` + our own `diskio.c` for the two SPI buses (`sdmm.c` sample as reference); `FF_VOLUMES = 2`. |
| `consoleio.*` | **Shim** | Keep the entire `console_*` API (everything above depends on it). Swap the backends: output → NTSC text driver; input → a key event queue fed by **both** PS/2 and USB-host drivers. ANSI handling can stay. |
| `random.*` | **Rewrite** | Single muxed RP2040 ADC sampling ch0/ch1 in sequence → **direct BLAKE2s extraction** (§1.3.2), streamed incrementally (no raw buffer). Apply §1.3.1: conservative per-sample credit, **very generous oversampling**, block-until-satisfied, stuck-source health check. Drops the Crypto-lib ChaCha `RNG`/DRBG in favor of the stateless extractor. |
| `SimpleTransistorNoiseSource.*` | **Drop** | No longer needed — the `NoiseSource` abstraction existed only to feed the Crypto-lib DRBG. Direct BLAKE2s extraction (§1.3.2) folds ADC capture + debias + credit directly into `random.*`. |
| `flashstruct.*` | **Rewrite** | STM32 flash controller → `hardware/flash.h` (`flash_range_erase`/`program`, 4 KB sectors). Dedicated key region at top of flash, outside the copied image (§1.4/§1.8). |
| `TNTSChar` / `TNTSCAnsi` | **New** | **Standalone `pico_ntsc` library** (§0.B, §2.2): PIO+DMA engine per §1.5 — 2 lockstep SMs (sync+luma), ping-pong DMA, sync templates, 1bpp framebuffer + glyph blitter, 40/80-col, runs on core1. Zero app coupling. `TNTSCAnsi`'s ANSI/cursor logic moves up into `consoleio`, not the library; reuse font data if usable. |
| `PS2Keyboard` | **New** | RP2040 PS/2 driver (PIO or IRQ bit-bang) on GPIO4/5, pushing into the shared key event queue. |
| — | **New (opt-in)** | **USB-host HID keyboard** driver (TinyUSB host) feeding the same key event queue. Compiled in **only** when `PICOPARANOIA_ENABLE_USB_HOST` is set (default **off**, §1.7); otherwise no USB code links and the keyboard is PS/2-only. |
| — | **New** | **Logging shim** (§1.7): build-time backend switch `uart0` (default) ↔ USB-CDC (`PICOPARANOIA_ENABLE_USB_STDIO`, default off) ↔ none. |
| `ParanoiaBox.ino` | **Rewrite** | Becomes `main.c`: SDK init, peripheral bring-up, then the existing menu/dispatch loop. |
| — | **New** | Board pin-definitions header (single source of truth for the §1.1 map). |
| — | **Build config** | `pico_set_binary_type(picoparanoia copy_to_ram)` so the image runs entirely from SRAM (§1.8); reserve a flash key region in the linker/flash layout. USB feature flags `PICOPARANOIA_ENABLE_USB_HOST` / `PICOPARANOIA_ENABLE_USB_STDIO` both default **off** (§1.7). |

### 2.1 Build / bring-up order (bottom-up, de-risk video early)

1. **Skeleton + crypto (copy-to-RAM from the start):** ✅ **DONE.** SDK project
   builds with `copy_to_ram` (verified: `.text` VMA in SRAM); vendored Crypto
   subset in `third_party/crypto` (GCM/AES256/CTR/BLAKE2s + deps, Curve25519
   deferred to step 5); `src/main.cpp` self-tests AES-256-GCM + BLAKE2s against
   known-answer vectors. Console is **USB-CDC for now** (temporary bring-up
   convenience — the §1.7 default of uart0 + USB-off is restored before USB-host
   support lands, which is itself deferred). RAM: ~70 KB used, ~194 KB free.
   *Remaining:* run on hardware to see the PASS/FAIL print; vendor FatFs R0.16
   into `third_party/fatfs` (not needed until step 6).
2. **Video out (highest risk):** NTSC text-cell driver on GPIO16/17. Get a
   character grid on a TV. Prototype this early — it gates the UI.
3. **Keyboard in:** start with the **PS/2** driver on GPIO4/5 (works while
   native USB stays in device/CDC mode) → completes the `consoleio` API →
   `editor.c` runs. Defer **USB-host** keyboard to step 9.
4. **RNG:** ADC noise capture on GPIO26/27 → `random` → seeds crypto. Build in
   the §1.3.1 discipline from the start: conservative credit + heavy
   oversampling + stuck-source health check. Sanity-test LSB randomness.
5. **Flash key store:** `flashstruct` rewrite → `keymanager` persists keys.
6. **SD cards:** dual-bus FatFs glue (spi1=ciphertext, spi0=plaintext) →
   `fileop` mounts both volumes.
7. **File crypto:** `fileenc` end-to-end (encrypt on plaintext card → ciphertext
   card and back).
8. **Integrate:** port the main menu loop; full system test. This is the
   **default shipping config** — USB disabled, PS/2 keyboard, `uart0` (or no)
   debug. The device is fully functional here without any USB code linked.
9. **USB-host keyboard (opt-in feature):** with `PICOPARANOIA_ENABLE_USB_HOST`,
   flip native USB to host mode and add the TinyUSB HID-host driver into the
   shared key queue. Debug stays on `uart0` (USB-CDC is unavailable in host
   mode). This feature is off by default; the device never depends on it.

### 2.2 Repo / library layout

Structured to keep the standalone video library (§0.B) and the security core
(§0.A) cleanly separated:

```
Code/
  pico_ntsc/          standalone, reusable video library — own CMake target
    pico_ntsc.h         clean public API: framebuffer + timing only, no app deps
    ...                 PIO programs, sync templates, glyph blitter
  src/                PicoParanoia firmware
    main.c
    consoleio.*         bridges pico_ntsc + keyboard to the app (not in the lib)
    crypto core:        cryptotool, keymanager, fileenc, random, flashstruct
    drivers:            ps2_kbd, usb_host_kbd, sd_spi glue, logging shim
  third_party/        vendored + pinned, each with version/license/provenance
    crypto/             Arduino Cryptography Library (recompiled, no Arduino glue)
    fatfs/              ElmChan FatFs (portable core + our RP2040 disk-IO glue)
  CMakeLists.txt      copy_to_ram binary type; links pico_ntsc + third_party
```

The crypto core has no dependency on `pico_ntsc` or the USB/SD stacks beyond the
defined data-path interfaces, so it can be audited in isolation.

### 2.3 Vendored dependencies (pinned, with provenance)

Per §0.A, every third-party component is vendored, pinned, and recorded:

| Dependency | Version / pin | License | Source | TCB role |
|------------|---------------|---------|--------|----------|
| Pico SDK | 2.2.0 (submodule) | BSD-3-Clause | github.com/raspberrypi/pico-sdk | platform |
| FatFs | **R0.16** (2025-07-22) | 1-clause BSD-style (ChaN) | elm-chan.org/fs/ff — staged in `Code/archives/ff16.zip` | filesystem (unavoidable) |
| Arduino Cryptography Library | **commit `37a76b8`** (2024-05-27) | MIT (Southern Storm / R. Weatherley) | github.com/rweather/arduinolibs | **security core** |
| TinyUSB | bundled with Pico SDK | MIT | — | optional (USB, §1.7) |

Notes:
- **FatFs R0.16** replaces the old `ElmChanFatFs` (the STM32 version shipped an
  R0.1x with STM32-specific glue). Vendor `source/` (`ff.c`, `ffunicode.c`,
  `ffsystem.c`, `ff.h`, `ffconf.h`, `diskio.h`) and write our own `diskio.c`
  for the two SPI buses. `ffsample/generic/sdmm.c` (from `ffsample.zip`) is a
  clean SD/MMC-over-SPI reference for that glue. Set `FF_VOLUMES = 2`.
- **Crypto lib — vendor only the needed subset** (minimize TCB): the project
  uses **GCM, AES256, CTR, BLAKE2s, Curve25519** plus their dependencies
  (`Cipher`/`BlockCipher`/`AuthenticatedCipher`, `AESCommon`, `GF128`/`GHASH`,
  `BigNumberUtil`, `Crypto.*`). Take only `libraries/Crypto/`, not the rest of
  the arduinolibs repo; drop `AESEsp32`, `examples/`, and Arduino-only pieces.
- **`RNGClass`/`ChaCha`/`NoiseSource` are excluded** (not just shimmed): the
  decision to use direct BLAKE2s extraction (§1.3.2) means the lib's DRBG — the
  only Arduino-coupled part (EEPROM/`millis`) and the only consumer of `ChaCha`
  here — never enters the build. One fewer primitive and zero Arduino RNG glue
  in the TCB.

## 3. Open questions

- ~~Composite-video signal scheme on GPIO16/17~~ — **resolved (§1.5):** 2-resistor
  DAC (560 Ω sync / 240 Ω luma), monochrome, 240p progressive, two free-running
  lockstep PIO SMs + ping-pong DMA, 1bpp framebuffer, 40/80-col.
- Whether the existing `TNTSCharfont` glyph data is reusable as-is for the new
  video driver (8×8 cells assumed; confirm dimensions/encoding).
- SD card supply voltage / level shifting on the two buses (hardware) and max
  workable SPI clock per bus.
- ~~Console stdio target~~ — **resolved (§1.7):** USB-CDC for bring-up, then it
  goes away when native USB becomes a keyboard host; persistent debug moves to
  `uart0` or is compiled out.
- USB-host scope: boot-protocol HID keyboard only, or also hubs / NKRO report
  protocol? (Boot keyboard is the simple, sufficient default.)
- Behavior when both a PS/2 and a USB keyboard are attached — just merge both
  into the queue, or pick one?
- RAM headroom under copy-to-RAM (§1.8): does the full build (code + video
  buffer + file buffers + USB host + crypto) fit comfortably in 264 KB, and how
  big do the file-encryption buffers need to be?
- Whether to actively quiesce/disable XIP + flash between encrypted-key accesses
  for maximum bus idleness (§1.8 hardening note).
- _(more as they arise)_
