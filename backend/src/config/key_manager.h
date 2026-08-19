#pragma once
#include "csilk/csilk.h"

/**
 * @brief Generate an RSA-2048 key pair and store it in the server process.
 *        Keys are valid for the lifetime of the process.
 * @return 0 on success, -1 on failure.
 */
int auth_key_init(void);

/**
 * @brief Return the PEM-encoded public key string (valid until process exit).
 */
const char* auth_key_get_public_pem(void);

/**
 * @brief Return the PEM-encoded private key string (valid until process exit).
 */
const char* auth_key_get_private_pem(void);

/**
 * @brief Handle GET /api/auth/public-key — returns JWK public key.
 */
void auth_public_key(csilk_ctx_t* c);
