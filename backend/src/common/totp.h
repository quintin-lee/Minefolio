#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Generate a random Base32 secret (32 characters + null terminator) */
int totp_generate_secret(char* out_secret, size_t cap);

/* Compute 6-digit TOTP code for a given timestamp and Base32 secret */
int totp_generate_code(const char* base32_secret, uint64_t timestamp, char* out_code, size_t cap);

/* Validate user-provided code against secret with window tolerance (±1 step = 30s) */
bool totp_verify_code(const char* base32_secret, const char* code);

/* Generate 8 alphanumeric backup codes (e.g. 1a2b-3c4d) */
int totp_generate_backup_codes(char out_codes[8][16]);

/* Verify and consume a backup code from JSON array string.
   Returns true if matched, and writes remaining codes JSON into out_updated_json */
bool totp_verify_and_consume_backup_code(const char* backup_codes_json,
                                         const char* input_code,
                                         char*       out_updated_json,
                                         size_t      cap);
