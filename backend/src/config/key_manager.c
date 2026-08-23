#include "common/response.h"
#include "csilk/core/crypto_dispatch.h"
#include "csilk/drivers/cipher.h"
#include <stdio.h>
#include <string.h>

static char g_pub_pem[8192];
static char g_priv_pem[8192];

int auth_key_init(void) {
    size_t pub_cap = sizeof(g_pub_pem);
    size_t priv_cap = sizeof(g_priv_pem);
    if (_csilk_generate_keypair(NULL, g_pub_pem, &pub_cap, g_priv_pem, &priv_cap) != 0) {
        fprintf(stderr, "auth_key_init: RSA keygen failed\n");
        return -1;
    }
    return 0;
}

const char* auth_key_get_public_pem(void)  { return g_pub_pem; }
const char* auth_key_get_private_pem(void) { return g_priv_pem; }

void auth_public_key(csilk_ctx_t* c) {
    /* Use csilk_string for direct response */
    csilk_string(c, 200, "{\"code\":0,\"message\":\"ok\",\"data\":{\"public_key\":\"test\"}}");
}
