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
)

# Public so dependents resolve <BLAKE2s.h>, <GCM.h>, "utility/..." etc.
target_include_directories(crypto_subset PUBLIC ${CMAKE_CURRENT_LIST_DIR})
