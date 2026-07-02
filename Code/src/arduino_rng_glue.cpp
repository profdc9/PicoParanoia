// arduino_rng_glue.cpp — implement the Arduino crypto library's RNGClass
// interface (RNG.h) on top of the PicoParanoia entropy extractor.
//
// The vendored Curve25519.cpp is used verbatim. Its dh1() — called by
// keymanager to generate a private scalar — does RNG.rand(f, 32). We do NOT
// compile the Arduino RNG implementation (the ChaCha CSPRNG + NoiseSource
// stack); instead we define RNGClass::rand() here and route it to
// randomness_get_whitened_bits(): direct ADC-noise + BLAKE2s extraction with
// the SP 800-90B health checks (PORTING.md §1.3.2). So dh1() keeps its audited
// logic (clamping, weak-point rejection) but draws entropy from our source.
//
// dh1() uses nothing else on RNGClass, so only rand(), the RNG global, and the
// (trivial) constructor/destructor are defined; the remaining RNGClass methods
// are intentionally left unimplemented and are never referenced.

#include "RNG.h"
#include "random.h"

RNGClass RNG;

RNGClass::RNGClass() {}
RNGClass::~RNGClass() {}

void RNGClass::rand(uint8_t *data, size_t len)
{
    randomness_get_whitened_bits(data, len);
}
