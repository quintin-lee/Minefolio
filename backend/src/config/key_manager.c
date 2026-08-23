#include "common/response.h"
#include "csilk/core/crypto_dispatch.h"
#include "csilk/drivers/cipher.h"
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <stdlib.h>
#include <string.h>

static char g_pub_pem[8192];
static char g_priv_pem[8192];
static size_t g_priv_pem_len = 0;

int auth_key_init(void) {
    size_t pub_cap = sizeof(g_pub_pem);
    size_t priv_cap = sizeof(g_priv_pem);
    if (_csilk_generate_keypair(NULL, g_pub_pem, &pub_cap, g_priv_pem, &priv_cap) != 0) {
        fprintf(stderr, "auth_key_init: RSA keygen failed\n");
        return -1;
    }
    g_priv_pem_len = strlen(g_priv_pem);
    return 0;
}

const char* auth_key_get_public_pem(void)  { return g_pub_pem; }
const char* auth_key_get_private_pem(void) { return g_priv_pem; }
size_t auth_key_get_private_pem_len(void)  { return g_priv_pem_len; }

void auth_public_key(csilk_ctx_t* c) {
    /* Return a static JWK for testing */
    const char* jwk = "{\"kty\":\"RSA\",\"n\":\"test\",\"e\":\"AQAB\"}";
    csilk_json_string(c, 200, jwk);
}
#pragma GCC diagnostic pop
