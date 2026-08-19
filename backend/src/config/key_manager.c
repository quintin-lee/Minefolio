
#include "common/response.h"
#include "csilk/core/codec.h"
#include "csilk/core/crypto_dispatch.h"
#include "csilk/drivers/cipher.h"
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdlib.h>
#include <string.h>

static char g_pub_pem[4096];
static char g_priv_pem[4096];

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

/* Extract JWK n,e from a PEM RSA private key. */
static int rsa_pubkey_to_jwk(const char* pem, size_t pem_len, char* jwk_buf, size_t jwk_cap) {
    BIO* bio = BIO_new_mem_buf(pem, (int)pem_len);
    if (!bio) return -1;
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) return -1;

    /* Use EVP_PKEY_get1_RSA (OpenSSL 3.x compatible despite deprecation warning)
     * to extract RSA-specific components n and e directly. */
    RSA* rsa = EVP_PKEY_get1_RSA(pkey);
    EVP_PKEY_free(pkey);
    if (!rsa) return -1;

    const BIGNUM* n = NULL;
    const BIGNUM* e = NULL;
    RSA_get0_key(rsa, &n, &e, NULL);
    if (!n || !e) { RSA_free(rsa); return -1; }

    int nlen = BN_num_bytes(n);
    int elen = BN_num_bytes(e);
    uint8_t* nbuf = (uint8_t*)malloc((size_t)nlen);
    uint8_t* ebuf = (uint8_t*)malloc((size_t)elen);
    if (!nbuf || !ebuf) {
        free(nbuf); free(ebuf); RSA_free(rsa); return -1;
    }
    BN_bn2bin(n, nbuf);
    BN_bn2bin(e, ebuf);
    RSA_free(rsa);

    /* Build JWK with stripped leading zeros */
    size_t ni = 0;
    while (ni < (size_t)nlen && nbuf[ni] == 0) ni++;
    size_t ei = 0;
    while (ei < (size_t)elen && ebuf[ei] == 0) ei++;

    char n_b64[360], e_b64[8];
    csilk_base64url_encode(nbuf + ni, (size_t)nlen - ni, n_b64);
    csilk_base64url_encode(ebuf + ei, (size_t)elen - ei, e_b64);
    free(nbuf);
    free(ebuf);

    int written = snprintf(jwk_buf, jwk_cap,
        "{\"kty\":\"RSA\",\"n\":\"%s\",\"e\":\"%s\"}", n_b64, e_b64);
    return (written > 0 && (size_t)written < jwk_cap) ? 0 : -1;
}

void auth_public_key(csilk_ctx_t* c) {
    char jwk[1024];
    /* Use the PRIVATE key PEM to derive the JWK (public key is embedded in it) */
    if (rsa_pubkey_to_jwk(
            auth_key_get_private_pem(), strlen(auth_key_get_private_pem()),
            jwk, sizeof(jwk)) != 0) {
        respond_error(c, 500, "public-key generation error");
        return;
    }
    csilk_json_t* resp = csilk_json_object();
    csilk_json_add_string(resp, "public_key", jwk);
    respond_ok(c, resp);
}

