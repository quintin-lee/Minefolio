/**
 * @file key_manager.c
 * @brief RSA 密钥对生命周期管理与非对称加解密实现
 *
 * 实现了 RSA-2048 密钥对在进程生命周期内的生成、PEM 与 JWK 格式转换、
 * 以及基于 RSA-OAEP / Base64URL 的传输层密码解密功能。
 */

#include "common/response.h"
#include "csilk/core/crypto_dispatch.h"
#include "csilk/drivers/cipher.h"
#include "csilk/core/codec.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief 内部静态变量：存储 PEM 格式公钥字符串
 */
static char g_pub_pem[8192];

/**
 * @brief 内部静态变量：存储 PEM 格式私钥字符串
 */
static char g_priv_pem[8192];

/**
 * @brief 生成 RSA-2048 密钥对并保存在内部静态缓冲区
 *
 * @return int 0 成功，-1 失败
 */
int
auth_key_init(void)
{
    size_t pub_cap = sizeof(g_pub_pem);
    size_t priv_cap = sizeof(g_priv_pem);
    if (_csilk_generate_keypair(NULL, g_pub_pem, &pub_cap, g_priv_pem, &priv_cap) != 0) {
        fprintf(stderr, "auth_key_init: RSA keygen failed\n");
        return -1;
    }
    return 0;
}

/**
 * @brief 获取内部 PEM 格式公钥字符串
 *
 * @return const char* PEM 公钥字符串指针
 */
const char*
auth_key_get_public_pem(void)
{
    return g_pub_pem;
}

/**
 * @brief 获取内部 PEM 格式私钥字符串
 *
 * @return const char* PEM 私钥字符串指针
 */
const char*
auth_key_get_private_pem(void)
{
    return g_priv_pem;
}

/**
 * @brief 将 OpenSSL 大数 (BIGNUM) 转换为 Base64URL 编码字符串
 *
 * 用于提取 RSA 模数 (n) 和公钥指数 (e) 生成 JWK 格式参数（去除末尾 padding '='，并将 '+' 替换为 '-'、'/' 替换为 '_'）。
 *
 * @param[in] bn OpenSSL BIGNUM 对象指针
 * @param[out] out 写入 Base64URL 结果的字符缓冲区
 * @param[in] out_cap 输出缓冲区的最大容量
 *
 * @return int 状态码
 * @retval 0 转换成功
 * @retval -1 内存分配失败或转换异常
 *
 * @note 内部辅助函数，动态申请 OpenSSL 缓冲区并负责释放。
 */
static int
bn_to_b64url(const BIGNUM* bn, char* out, size_t out_cap)
{
    int            len = BN_num_bytes(bn);
    unsigned char* buf = OPENSSL_malloc(len);
    if (!buf) {
        return -1;
    }
    BN_bn2bin(bn, buf);

    int   b64_len = EVP_ENCODE_LENGTH(len);
    char* b64 = OPENSSL_malloc(b64_len);
    if (!b64) {
        OPENSSL_free(buf);
        return -1;
    }

    int enc_len = EVP_EncodeBlock((unsigned char*)b64, buf, len);
    OPENSSL_free(buf);

    size_t j = 0;
    for (int i = 0; i < enc_len && j < out_cap - 1; i++) {
        if (b64[i] == '+') {
            out[j++] = '-';
        } else if (b64[i] == '/') {
            out[j++] = '_';
        } else if (b64[i] != '=') {
            out[j++] = b64[i];
        }
    }
    out[j] = '\0';
    OPENSSL_free(b64);
    return 0;
}

/**
 * @brief 将 PEM 格式的公钥解析并转换为 JWK (JSON Web Key) 格式对象
 *
 * 读取 PEM 字符串，利用 OpenSSL EVP_PKEY 接口提取模数 (n) 和公钥指数 (e)，
 * 构造成 JSON 对象 {"kty": "RSA", "n": "...", "e": "..."}。
 *
 * @param[in] pem PEM 格式公钥字符串
 *
 * @return csilk_json_t* 指向新创建的 JSON 对象指针；解析失败返回 NULL
 *
 * @note 内存所有权：返回的 JSON 对象需由调用方负责释放或挂载到响应树中统一释放。
 */
static csilk_json_t*
pem_to_jwk(const char* pem)
{
    BIO* bio = BIO_new_mem_buf(pem, -1);
    if (!bio) {
        return NULL;
    }

    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) {
        return NULL;
    }

    BIGNUM* n = NULL;
    BIGNUM* e = NULL;

    if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_N, &n) != 1 ||
        EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_RSA_E, &e) != 1) {
        BN_free(n);
        BN_free(e);
        EVP_PKEY_free(pkey);
        return NULL;
    }

    char n_str[512] = {0};
    char e_str[64] = {0};
    if (bn_to_b64url(n, n_str, sizeof(n_str)) != 0 || bn_to_b64url(e, e_str, sizeof(e_str)) != 0) {
        BN_free(n);
        BN_free(e);
        EVP_PKEY_free(pkey);
        return NULL;
    }

    csilk_json_t* jwk = csilk_json_object();
    csilk_json_add_string(jwk, "kty", "RSA");
    csilk_json_add_string(jwk, "n", n_str);
    csilk_json_add_string(jwk, "e", e_str);

    BN_free(n);
    BN_free(e);
    EVP_PKEY_free(pkey);
    return jwk;
}

/**
 * @brief GET /api/auth/public-key 接口处理函数
 *
 * @param[in,out] c HTTP 请求上下文
 */
void
auth_public_key(csilk_ctx_t* c)
{
    const char*   pub_pem = auth_key_get_public_pem();
    csilk_json_t* jwk = pem_to_jwk(pub_pem);
    if (!jwk) {
        respond_error(c, 500, "Failed to export public key");
        return;
    }
    csilk_json_t* data = csilk_json_object();
    csilk_json_add_object(data, "public_key", jwk);
    respond_ok(c, data);
}

/**
 * @brief 使用私钥解密前端加密的 Base64URL 密文
 *
 * @param[in] ciphertext_b64url Base64URL 编码密文
 * @param[out] out_buf 接收解密结果的缓冲区
 * @param[in,out] out_len 输入缓冲区容量，输出实际明文字节数
 *
 * @return int 0 成功，-1 失败
 */
int
auth_key_decrypt(const char* ciphertext_b64url, char* out_buf, size_t* out_len)
{
    if (!ciphertext_b64url || !out_buf || !out_len || *out_len == 0) {
        return -1;
    }
    uint8_t ct_buf[CSILK_RSA_KEY_SIZE];
    if (csilk_base64url_decode(ciphertext_b64url, ct_buf, sizeof(ct_buf)) < 0) {
        return -1;
    }
    size_t pt_len = *out_len - 1;
    if (_csilk_asymmetric_decrypt(NULL,
                                  g_priv_pem,
                                  strlen(g_priv_pem),
                                  ct_buf,
                                  CSILK_RSA_KEY_SIZE,
                                  (uint8_t*)out_buf,
                                  &pt_len) != 0 ||
        pt_len == 0) {
        return -1;
    }
    out_buf[pt_len] = '\0';
    *out_len = pt_len;
    return 0;
}
