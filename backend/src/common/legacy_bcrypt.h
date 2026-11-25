#pragma once

#include <stddef.h>

/**
 * @brief Verify a plaintext password against a legacy 61-character bcrypt hash
 *        produced by csilk < v1.2.0.
 *
 * Pre-v1.2.0 csilk produced 61-character hashes (`$2a$XX$<22 salt><32 cipher>`)
 * using an earlier Blowfish/Eksblowfish implementation that is incompatible with
 * csilk v1.2.0's OpenBSD bcrypt algorithm.
 *
 * @param password Plaintext password bytes (truncated to 72 bytes).
 * @param len      Byte length of @p password.
 * @param hash     Stored NUL-terminated bcrypt hash string (must be 61 chars).
 * @return 0 if the password matches @p hash, -1 otherwise.
 */
int mf_legacy_bcrypt_v090_verify(const char* password, size_t len, const char* hash);
