# Vendored: Arduino Cryptography Library (subset)

- **Upstream:** https://github.com/rweather/arduinolibs (`libraries/Crypto`)
- **Author:** Rhys Weatherley / Southern Storm Software, Pty Ltd.
- **Pinned commit:** `37a76b8f7516568e1c575b6dc9268da1ccaac6b6` (2024-05-27)
- **License:** MIT (per-file headers; see `LICENSE`)
- **Modifications:** none — files are copied **verbatim** from upstream.

## Why a subset (auditability, PORTING.md §0.A / §2.3)

Only the primitives PicoParanoia uses are vendored, to keep the trusted
computing base small. The rest of the arduinolibs repo and the unused Crypto
modules (SHA-2/3, ChaCha/Poly1305, EAX/OMAC/XTS, Ed25519, P521, HKDF, the
ESP32 AES path, examples, …) are deliberately excluded.

## Files in this directory (step-1 set)

AEAD / cipher path used by `cryptotool` (AES-256-GCM, AES-CTR):
`AES.h`, `AESCommon.cpp`, `AES256.cpp`, `BlockCipher.[ch]`, `Cipher.[ch]`,
`AuthenticatedCipher.[ch]`, `GCM.[ch]`, `GHASH.[ch]`, `GF128.[ch]`, `CTR.[ch]`,
plus the core `Crypto.[ch]`, `Hash.[ch]`, hash `BLAKE2s.[ch]`, and
`utility/{EndianUtil,LimbUtil,ProgMemUtil,RotateUtil}.h`.

## Deliberately NOT included yet

- **`Curve25519` + `BigNumberUtil`** (ECDH25519) are added at bring-up **step 5**
  (keymanager), together with a small patch to drop `Curve25519.cpp`'s
  `#include "RNG.h"` / `dh1()`/`dh2()` — we generate the private scalar with our
  own BLAKE2s entropy extractor (PORTING.md §1.3.2) and call `eval()` directly.
- **`RNGClass` / `ChaCha` / `NoiseSource`** — excluded entirely (the only
  Arduino-coupled code; replaced by direct BLAKE2s extraction, §1.3.2).

`CRYPTO_AES_ESP32` is never defined, so `AES256` uses the software `AESCommon`
implementation (not `AESEsp32.cpp`, which is not vendored).
