# Vendored: Arduino Cryptography Library (subset)

- **Upstream:** https://github.com/rweather/arduinolibs (`libraries/Crypto`)
- **Author:** Rhys Weatherley / Southern Storm Software, Pty Ltd.
- **Pinned commit:** `37a76b8f7516568e1c575b6dc9268da1ccaac6b6` (2024-05-27)
- **License:** MIT (per-file headers; see `LICENSE`)
- **Modifications:** none — files are copied **verbatim** from upstream.

## Why a subset (auditability, PORTING.md §0.A / §2.3)

Only the primitives PicoParanoia uses are vendored, to keep the trusted
computing base small. The rest of the arduinolibs repo and the unused Crypto
modules (SHA-2/3, EAX/OMAC/XTS, Ed25519, P521, HKDF, the ESP32 AES path,
examples, …) are deliberately excluded.

## Files in this directory

AEAD / cipher path used by `cryptotool` (AES-256-GCM, AES-CTR):
`AES.h`, `AESCommon.cpp`, `AES256.cpp`, `BlockCipher.[ch]`, `Cipher.[ch]`,
`AuthenticatedCipher.[ch]`, `GCM.[ch]`, `GHASH.[ch]`, `GF128.[ch]`, `CTR.[ch]`,
plus the core `Crypto.[ch]`, `Hash.[ch]`, hash `BLAKE2s.[ch]`, and
`utility/{EndianUtil,LimbUtil,ProgMemUtil,RotateUtil}.h`.

ECDH25519 (keymanager): `Curve25519.[ch]`, `BigNumberUtil.[ch]`, and `RNG.h`.

ChaCha20-Poly1305 (candidate replacement for AES-256-GCM as the AEAD backing
`symmetric_memcrypt()`, PORTING.md-adjacent — see cryptotool.h's SYMMETRIC_*
naming): `ChaCha.[ch]`, `ChaChaPoly.[ch]`, `Poly1305.[ch]`. Same 256-bit key /
96-bit IV / 128-bit tag as GCM, so `ChaChaPoly` (which inherits
`AuthenticatedCipher`, same as `GCMCommon`) is close to drop-in. **Vendored
and KAT-validated only** (the RFC 8439 §2.8.2 "Sunscreen" vector, in
`main.cpp`'s `crypto_selftest()`) -- not yet wired into `symmetric_memcrypt()`.
One real gotcha for whoever does that swap: `ChaChaPoly::ivSize()` returns
`8` (the original 64-bit-nonce variant) by default, not `12`. `setIV()` must
be called with an explicit length of `12` to get the IETF/RFC 8439 96-bit
nonce; `symmetric_memcrypt()`'s current `cipher.setIV(iv, cipher.ivSize())`
pattern (fine for GCM, whose `ivSize()` correctly returns 12) would silently
pick the wrong nonce size for ChaChaPoly if reused unchanged.

## ECDH25519 without vendoring the Arduino RNG

`Curve25519.cpp` `#include`s `"RNG.h"`, and its `dh1()` calls `RNG.rand()` to
generate the private scalar. We do **not** want the Arduino RNG (ChaCha CSPRNG +
`NoiseSource`); PicoParanoia derives entropy by direct BLAKE2s extraction from
the ADC noise sources (PORTING.md §1.3.2). Rather than *edit* the library — which
would break its audit provenance — we leave every file byte-for-byte identical to
upstream and arrange for `dh1()` to be unused:

- `RNG.h` is vendored **verbatim**. It is self-contained (only `<inttypes.h>`,
  `<stddef.h>`, a forward-declared `class NoiseSource`, and pure declarations),
  so it lets `Curve25519.cpp` compile.
- `RNG.cpp` is **not** compiled, so `RNGClass` / the `RNG` global are never
  defined or linked.
- keymanager never calls `dh1()`. It generates + clamps the private scalar with
  the platform extractor and calls `Curve25519::eval()` directly for keygen, and
  `Curve25519::dh2()` (which does not use `RNG`) for the shared secret.
- With `-ffunction-sections` + `--gc-sections` (Pico SDK defaults) the unused
  `dh1()` section is garbage-collected, so its `RNG.rand()` reference disappears
  and nothing pulls in `RNGClass`. The build fails to link if `dh1()` ever
  becomes reachable — a useful tripwire. The Curve25519 KAT in `main.cpp`
  references `eval()`, forcing the library into the link so this is exercised.

## Deliberately excluded

- **`RNG.cpp` / `NoiseSource.*`** — the Arduino-coupled RNG stack (only
  `RNG.h` is present, for compilation as described above). `RNG.cpp`'s own
  CSPRNG happens to be built on the same `ChaCha` class now vendored for
  `ChaChaPoly` (a different, legitimate consumer) -- vendoring `ChaCha.cpp`
  does not change whether `RNG.cpp` is compiled; it still isn't.
- Unused Crypto modules (SHA-2/3, EAX/OMAC/XTS, Ed25519, P521, HKDF, the
  ESP32 AES path, examples, …).

`CRYPTO_AES_ESP32` is never defined, so `AES256` uses the software `AESCommon`
implementation (not `AESEsp32.cpp`, which is not vendored).
