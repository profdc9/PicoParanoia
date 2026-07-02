# Vendored subset of the Arduino Cryptography Library (see PROVENANCE.md).
# Pure-software primitives; no Pico SDK or Arduino dependency.
add_library(crypto_subset STATIC
    Crypto.cpp
    Hash.cpp
    BLAKE2s.cpp
    Cipher.cpp
    BlockCipher.cpp
    AuthenticatedCipher.cpp
    AESCommon.cpp
    AES256.cpp
    GF128.cpp
    GHASH.cpp
    GCM.cpp
    CTR.cpp
    Curve25519.cpp
    BigNumberUtil.cpp
    ChaCha.cpp
    ChaChaPoly.cpp
    Poly1305.cpp
)

# NOTE: RNG.cpp is deliberately NOT compiled. Curve25519.cpp #includes "RNG.h"
# (vendored verbatim) and its dh1() calls RNG.rand(); keymanager uses dh1() for
# keygen, so we DON'T drop it. Instead src/arduino_rng_glue.cpp provides
# RNGClass::rand() backed by the platform BLAKE2s entropy extractor, so the
# Arduino ChaCha/NoiseSource RNG stack is never linked while dh1() stays intact.
# The library files remain byte-for-byte identical to upstream (see PROVENANCE).

# Public so dependents resolve <BLAKE2s.h>, <GCM.h>, "utility/..." etc.
target_include_directories(crypto_subset PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# The project default is -Os (copy-to-RAM: code size is RAM; see top-level
# CMakeLists). Crypto is the speed-sensitive path — AES/GCM for file throughput,
# BLAKE2s for the KDF and RNG extraction — so this library is the exception: -O2.
# -ffunction-sections/-fdata-sections lets --gc-sections drop the crypto we don't
# call (e.g. unused Curve25519 DH helpers) from the RAM image.
target_compile_options(crypto_subset PRIVATE -O2 -ffunction-sections -fdata-sections)

# Exception within the exception: Curve25519's field arithmetic inlines/unrolls
# into ~135 KB at -O2/-O3. ECDH keygen/DH is rare, so keep just this file at -Os
# (~15 KB). -Os follows -O2 on the command line, so it wins here. Source stays
# byte-for-byte upstream.
set_source_files_properties(Curve25519.cpp PROPERTIES COMPILE_OPTIONS "-Os")
