# PicoParanoia — Porting Plan (STM32 ParanoiaBox → RP2040 Pico)

Status: **planning, no code yet.**

Porting the ParanoiaBox encryption terminal (`~/projects/ParanoiaBox/code`,
STM32F103CBT6 / stm32duino) to the Raspberry Pi Pico (RP2040), built on the
Pico SDK (vendored at `../pico-sdk`, v2.2.0).

Decision: keep the **Arduino Cryptography Library** (Rhys Weatherley). It is
pure software except for the RNG noise source, which we wire to the RP2040 ADC.

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
output then feeds the Arduino Crypto library's `RNG` whitening, same as before.

### 1.4 Internal flash key storage: STM32 flash controller → RP2040 QSPI

STM32 `flashstruct.cpp` programmed the on-die flash via `FLASH_BASE->KEYR`
unlock + page erase, and `keymanager.h` hardcodes `0x0801E000` (STM32 memory
map). The RP2040 has **no internal flash** — code/data live in external QSPI
flash. Key storage must move to the SDK's `hardware/flash.h`
(`flash_range_erase` / `flash_range_program`, 4 KB sectors, XIP-aware: must run
from RAM with interrupts disabled during writes). The hardcoded address and the
16-bit half-word write granularity assumptions go away.

### 1.5 NTSC/PAL video generation (largest unknown)

STM32 used the `TNTSChar` library, which generated composite video by abusing
an SPI peripheral + timers (libmaple). On RP2040, both SPI controllers are now
spoken for by the SD cards, so composite video is generated with **PIO + DMA**
instead.

**Resolved (2026-06-30):**
- **Text cells only** — a character-cell screen buffer (rows × cols of glyphs
  rendered from a font ROM), same data model as the old `TNTSChar`. No
  general-purpose pixel framebuffer.
- **NTSC first**, PAL later. PAL is expected to be a minor change: different
  line/field counts and timing constants, same PIO/DMA architecture. Keep the
  timing parameters (lines per field, active lines, color/sync timings) in one
  place so PAL is a table swap, not a rewrite.

Signal: two GPIOs only — GPIO16 (VSYNC) and GPIO17 (pixel clock / luma data).
Working assumption: a PIO program clocks serialized pixel/luma bits out on
GPIO17 while sync is driven on GPIO16, the two combined through an external
resistor network into the composite levels (sync / black / white). *(Confirm
the exact resistor scheme when we reach the driver — it only affects the
lowest-level output stage, not the architecture.)*

This is still the largest single piece of new code (PIO program + DMA
scanout + glyph renderer + timing tables), but the scope is now bounded.

### 1.6 PS/2 keyboard

Was the `PS2Keyboard` lib bit-banging `PB4`/`PB5`. Same protocol, new pins
(GPIO4 data, GPIO5 clk). Candidates: a small RP2040 PIO program for the PS/2
clock/data framing, or a polled/IRQ GPIO bit-bang. Low risk.

### 1.7 Toolchain / framework

stm32duino Arduino core → bare-metal Pico SDK (CMake, `arm-none-eabi`). No
`Arduino.h`, `Serial`, libmaple, or stm32duino `SPI`/`RNG` glue. `printf`
routes to USB-CDC or UART. Crypto lib must be compiled as plain C++ against the
SDK rather than the Arduino build.

---

## 2. Module port strategy

Legend: **Reuse** = compiles essentially unchanged · **Shim** = keep the
module's API/logic, swap the platform calls underneath · **Rewrite** = replace
the implementation · **New** = no STM32 counterpart.

| Module (old) | Disposition | Work |
|--------------|-------------|------|
| `mini-printf.*` | **Reuse** | Pure C string formatting. |
| `debugmsg.*` | **Reuse / trim** | Route output to USB-CDC or `uart0` instead of `Serial`. |
| `cryptotool.*` | **Reuse** | AES/GCM/BLAKE2s via Crypto lib, base64, CRC16, KDF — all software. Only `heap_stack_distance()` (uses `sbrk`/frame address) needs an SDK-friendly version or removal. |
| Crypto library | **Reuse (recompile)** | Compile Weatherley's lib as plain C++ against the SDK; drop the Arduino/`RNG.h` glue, supply our own RNG seeding. |
| `editor.c` | **Reuse** | Talks only to the `consoleio` API; works once console is up. |
| `keymanager.*` | **Shim** | Logic is portable. Replace the flash-storage backing (see `flashstruct`) and the hardcoded `0x0801E000` address with an RP2040 QSPI region. |
| `fileenc.*` | **Reuse** | Pure logic over `keymanager` + `fileop` + `cryptotool`. |
| `fileop.*` | **Shim** | FatFs calls stay; the disk-IO layer underneath becomes two independent SPI/SD contexts (see below). `fs0`=ciphertext, `fs1`=plaintext. |
| FatFs (`ElmChanFatFs`) | **Reuse + new glue** | `ff.c`/`ffunicode.c` are portable. Replace `mmc_stm32f1_spi.c` with RP2040 SD-over-SPI glue; configure `_VOLUMES=2` for the two buses. |
| `consoleio.*` | **Shim** | Keep the entire `console_*` API (everything above depends on it). Swap the two backends: output → NTSC text driver; input → PS/2 driver. ANSI handling can stay. |
| `random.*` | **Rewrite (lower half)** | Replace dual-ADC register pokes with single muxed RP2040 ADC sampling ch0/ch1 in sequence; keep the whitening/`RNG` interface. |
| `SimpleTransistorNoiseSource.*` | **Shim** | Keep the `NoiseSource` subclass; point `stir()` at the new ADC capture. |
| `flashstruct.*` | **Rewrite** | STM32 flash controller → `hardware/flash.h` (`flash_range_erase`/`program`, 4 KB sectors, run-from-RAM + IRQs off, XIP-aware). |
| `TNTSChar` / `TNTSCAnsi` | **New** | PIO+DMA NTSC text-cell video driver + glyph renderer + timing tables (see §1.5). Reuse the font data if usable. |
| `PS2Keyboard` | **New** | RP2040 PS/2 driver (PIO program or IRQ bit-bang) on GPIO4/5, exposing the same getkey interface `consoleio` expects. |
| `ParanoiaBox.ino` | **Rewrite** | Becomes `main.c`: SDK init, peripheral bring-up, then the existing menu/dispatch loop. |
| — | **New** | `stdio` over USB-CDC (and/or `uart0`) to replace `Serial`; board pin-definitions header. |

### 2.1 Build / bring-up order (bottom-up, de-risk video early)

1. **Skeleton + crypto:** SDK project building; compile the Crypto lib and
   `cryptotool`; self-test AES-GCM/BLAKE2s over USB serial. (No custom HW.)
2. **Video out (highest risk):** NTSC text-cell driver on GPIO16/17. Get a
   character grid on a TV. Prototype this early — it gates the UI.
3. **Keyboard in:** PS/2 driver on GPIO4/5 → completes the `consoleio` API →
   `editor.c` runs.
4. **RNG:** ADC noise capture on GPIO26/27 → `random` → seeds crypto.
5. **Flash key store:** `flashstruct` rewrite → `keymanager` persists keys.
6. **SD cards:** dual-bus FatFs glue (spi1=ciphertext, spi0=plaintext) →
   `fileop` mounts both volumes.
7. **File crypto:** `fileenc` end-to-end (encrypt on plaintext card → ciphertext
   card and back).
8. **Integrate:** port the main menu loop; full system test.

## 3. Open questions

- Exact composite-video resistor/output scheme on GPIO16/17 (§1.5) — needed at
  step 2, not before.
- Whether the existing `TNTSCharfont` glyph data is reusable as-is for the new
  video driver.
- SD card supply voltage / level shifting on the two buses (hardware) and max
  workable SPI clock per bus.
- Console stdio target: USB-CDC, `uart0` (GPIO0/1), or both?
- _(more as they arise)_
