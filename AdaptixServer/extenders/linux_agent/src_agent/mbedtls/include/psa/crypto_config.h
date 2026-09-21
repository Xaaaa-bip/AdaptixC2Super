/*
 * crypto_config.h — trimmed PSA capability surface (linux_agent H1).
 * Replaces the stock file (whose WANT list is unconditional) with the
 * minimal set the TLS1.3 client needs: HKDF + SHA-256/384 + ECDH + ECDSA
 * + AES-GCM/CCM/ChaCha20-Poly1305. No JPAKE/FFDH/RSA/DES/MD5.
 */
#ifndef PSA_CRYPTO_CONFIG_H
#define PSA_CRYPTO_CONFIG_H

#define PSA_WANT_ALG_SHA_256            1
#define PSA_WANT_ALG_SHA_384            1
#define PSA_WANT_ALG_HKDF_EXTRACT       1
#define PSA_WANT_ALG_HKDF_EXPAND        1
#define PSA_WANT_ALG_HMAC               1
#define PSA_WANT_ALG_GCM                1
#define PSA_WANT_ALG_CCM                1
#define PSA_WANT_ALG_CHACHA20_POLY1305  1
#define PSA_WANT_ALG_ECDH               1
#define PSA_WANT_ALG_ECDSA              1
#define PSA_WANT_KEY_TYPE_HMAC          1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR  1
#define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY 1
#define PSA_WANT_ECC_SECP_R1_256        1
#define PSA_WANT_ECC_SECP_R1_384        1

#endif /* PSA_CRYPTO_CONFIG_H */
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC      1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT     1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_EXPORT     1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE   1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_DERIVE     1
#define PSA_WANT_KEY_TYPE_RAW_DATA                1
#define PSA_WANT_KEY_TYPE_AES                     1
#define PSA_WANT_KEY_TYPE_CHACHA20                1
