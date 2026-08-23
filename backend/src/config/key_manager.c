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

/* Base64url encode manually */
static void base64url_encode(const uint8_t* src, size_t len, char* dst) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t i = 0, j = 0;
    uint32_t val = 0;
    int bit = 0;
    
    for (i = 0; i < len; i++) {
        val = (val << 8) + src[i];
        bit += 8;
        while (bit >= 6) {
            bit -= 6;
            dst[j++] = table[(val >> bit) & 0x3f];
        }
    }
    if (bit > 0) {
        dst[j++] = table[(val << (6 - bit)) & 0x3f];
    }
    dst[j] = '\0';
}

/* Extract JWK n,e from RSA public key */
static int rsa_pubkey_to_jwk(const char* pub_pem, size_t pub_pem_len, char* jwk_buf, size_t jwk_cap) {
    BIO* bio = BIO_new_mem_buf(pub_pem, (int)pub_pem_len);
    if (!bio) return -1;
    
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) return -1;
    
    const RSA* rsa = EVP_PKEY_get0_RSA(pkey);
    EVP_PKEY_free(pkey);
    if (!rsa) return -1;
    
    const BIGNUM* n = NULL;
    const BIGNUM* e = NULL;
    RSA_get0_key(rsa, &n, &e, NULL);
    if (!n || !e) return -1;
    
    int nlen = BN_num_bytes(n);
    int elen = BN_num_bytes(e);
    uint8_t* nbuf = (uint8_t*)malloc((size_t)nlen);
    uint8_t* ebuf = (uint8_t*)malloc((size_t)elen);
    if (!nbuf || !ebuf) {
        free(nbuf); free(ebuf); return -1;
    }
    BN_bn2bin(n, nbuf);
    BN_bn2bin(e, ebuf);
    
    /* Strip leading zeros */
    size_t ni = 0;
    while (ni < (size_t)nlen && nbuf[ni] == 0) ni++;
    size_t ei = 0;
    while (ei < (size_t)elen && ebuf[ei] == 0) ei++;
    
    char n_b64[360], e_b64[8];
    base64url_encode(nbuf + ni, (size_t)nlen - ni, n_b64);
    base64url_encode(ebuf + ei, (size_t)elen - ei, e_b64);
    free(nbuf);
    free(ebuf);
    
    int written = snprintf(jwk_buf, jwk_cap,
        "{\"kty\":\"RSA\",\"n\":\"%s\",\"e\":\"%s\"}", n_b64, e_b64);
    return (written > 0 && (size_t)written < jwk_cap) ? 0 : -1;
}

void auth_public_key(csilk_ctx_t* c) {
    char jwk[1024];
    if (rsa_pubkey_to_jwk(
            auth_key_get_public_pem(), strlen(auth_key_get_public_pem()),
            jwk, sizeof(jwk)) != 0) {
        csilk_json_string(c, 500, "{\"code\":500,\"message\":\"public-key generation error\"}");
        return;
    }
    csilk_json_string(c, 200, jwk);
}
#pragma GCC diagnostic pop
