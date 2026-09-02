/**
 * @file totp.c
 * @brief 基于时间的一次性密码 (TOTP, RFC 6238) 与双因子认证 (2FA) 实现
 *
 * 实现了 Base32 解码、HMAC-SHA1 动态码生成、滑动窗口比对、
 * 以及备用恢复码的随机生成和 JSON 列表原子消费逻辑。
 */

#include "common/totp.h"
#include "csilk/csilk.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/**
 * @brief Base32 编码标准字符集 (RFC 4648)
 */
static const char BASE32_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/**
 * @brief 将单个 Base32 字符解码为其对应的 5-bit 数值
 *
 * @param[in] c 输入字符
 * @return int 0~31 对应数值，非法字符返回 -1
 */
static int
base32_decode_char(char c)
{
    char u = (char)toupper((unsigned char)c);
    if (u >= 'A' && u <= 'Z') {
        return u - 'A';
    }
    if (u >= '2' && u <= '7') {
        return u - '2' + 26;
    }
    return -1;
}

/**
 * @brief 将 Base32 编码的字符串解码为原始字节流
 *
 * @param[in] encoded Base32 编码字符串
 * @param[out] out_bytes 输出字节缓冲区
 * @param[in] max_out 输出缓冲区容量
 * @param[out] out_len 实际解码写入的字节长度
 *
 * @return int 0 成功，-1 失败
 */
static int
base32_decode(const char* encoded, uint8_t* out_bytes, size_t max_out, size_t* out_len)
{
    if (!encoded || !out_bytes || max_out == 0) {
        return -1;
    }

    size_t in_len = strlen(encoded);
    size_t out_idx = 0;
    int    buffer = 0;
    int    bits_left = 0;

    for (size_t i = 0; i < in_len; i++) {
        char c = encoded[i];
        if (c == '=' || isspace((unsigned char)c)) {
            continue;
        }
        int val = base32_decode_char(c);
        if (val < 0) {
            continue;
        }

        buffer = (buffer << 5) | val;
        bits_left += 5;

        if (bits_left >= 8) {
            bits_left -= 8;
            if (out_idx >= max_out) {
                return -1;
            }
            out_bytes[out_idx++] = (uint8_t)((buffer >> bits_left) & 0xFF);
        }
    }

    if (out_len) {
        *out_len = out_idx;
    }
    return 0;
}

/**
 * @brief 生成 32 字符长度的随机 Base32 TOTP 密钥
 *
 * @param[out] out_secret 接收生成的 Base32 密钥字符串
 * @param[in] cap 输出缓冲区容量（至少 33 字节）
 * @return int 0 成功，-1 失败
 */
int
totp_generate_secret(char* out_secret, size_t cap)
{
    if (!out_secret || cap < 33) {
        return -1;
    }

    uint8_t raw[20];
    if (RAND_bytes(raw, sizeof(raw)) != 1) {
        /* 回退到 /dev/urandom */
        FILE* f = fopen("/dev/urandom", "rb");
        if (f) {
            size_t r = fread(raw, 1, sizeof(raw), f);
            fclose(f);
            if (r != sizeof(raw)) {
                return -1;
            }
        } else {
            return -1;
        }
    }

    /* 编码 20 字节原始熵 -> 32 个 Base32 字符 */
    size_t out_idx = 0;
    int    buffer = 0;
    int    bits_left = 0;

    for (size_t i = 0; i < sizeof(raw) && out_idx < 32 && out_idx + 1 < cap; i++) {
        buffer = (buffer << 8) | raw[i];
        bits_left += 8;
        while (bits_left >= 5 && out_idx < 32 && out_idx + 1 < cap) {
            bits_left -= 5;
            int idx = (buffer >> bits_left) & 0x1F;
            out_secret[out_idx++] = BASE32_ALPHABET[idx];
        }
    }
    if (bits_left > 0 && out_idx < 32 && out_idx + 1 < cap) {
        int idx = (buffer << (5 - bits_left)) & 0x1F;
        out_secret[out_idx++] = BASE32_ALPHABET[idx];
    }
    out_secret[out_idx] = '\0';
    return 0;
}

/**
 * @brief 计算给定时间戳的 6 位 TOTP 动态验证码
 *
 * @param[in] base32_secret Base32 密钥
 * @param[in] timestamp Unix 时间戳
 * @param[out] out_code 接收验证码的缓冲区
 * @param[in] cap 缓冲区容量（至少 7 字节）
 * @return int 0 成功，-1 失败
 */
int
totp_generate_code(const char* base32_secret, uint64_t timestamp, char* out_code, size_t cap)
{
    if (!base32_secret || !out_code || cap < 7) {
        return -1;
    }

    uint8_t secret_bytes[64];
    size_t  secret_len = 0;
    if (base32_decode(base32_secret, secret_bytes, sizeof(secret_bytes), &secret_len) != 0 ||
        secret_len == 0) {
        return -1;
    }

    uint64_t step = timestamp / 30;
    uint8_t  time_bytes[8];
    for (int i = 7; i >= 0; i--) {
        time_bytes[i] = (uint8_t)(step & 0xFF);
        step >>= 8;
    }

    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int  md_len = 0;

    if (!HMAC(EVP_sha1(), secret_bytes, (int)secret_len, time_bytes, 8, md, &md_len) ||
        md_len < 20) {
        return -1;
    }

    int      offset = md[md_len - 1] & 0x0F;
    uint32_t binary = ((md[offset] & 0x7F) << 24) | ((md[offset + 1] & 0xFF) << 16) |
                      ((md[offset + 2] & 0xFF) << 8) | (md[offset + 3] & 0xFF);

    uint32_t code_num = binary % 1000000;
    snprintf(out_code, cap, "%06u", code_num);
    return 0;
}

/**
 * @brief 校验 TOTP 验证码（容差 ±30 秒）
 *
 * @param[in] base32_secret Base32 密钥
 * @param[in] code 用户输入的验证码
 * @return bool true 匹配，false 不匹配
 */
bool
totp_verify_code(const char* base32_secret, const char* code)
{
    if (!base32_secret || !base32_secret[0] || !code || strlen(code) != 6) {
        return false;
    }

    uint64_t now = (uint64_t)time(NULL);

    /* 允许当前步长及前后 ±1 步长（30 秒容差） */
    for (int step = -1; step <= 1; step++) {
        uint64_t t = (int64_t)now + (step * 30);
        char     expected[8];
        if (totp_generate_code(base32_secret, t, expected, sizeof(expected)) == 0) {
            if (strcmp(expected, code) == 0) {
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief 生成 8 组字母数字混合的备用恢复码
 *
 * @param[out] out_codes 8x16 字符数组
 * @return int 0 成功，-1 失败
 */
int
totp_generate_backup_codes(char out_codes[8][16])
{
    const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    uint8_t    raw[64];
    if (RAND_bytes(raw, sizeof(raw)) != 1) {
        for (size_t i = 0; i < sizeof(raw); i++) {
            raw[i] = (uint8_t)rand();
        }
    }

    int raw_idx = 0;
    for (int i = 0; i < 8; i++) {
        char buf[16];
        for (int j = 0; j < 4; j++) {
            buf[j] = charset[raw[raw_idx++] % (sizeof(charset) - 1)];
        }
        buf[4] = '-';
        for (int j = 5; j < 9; j++) {
            buf[j] = charset[raw[raw_idx++] % (sizeof(charset) - 1)];
        }
        buf[9] = '\0';
        snprintf(out_codes[i], 16, "%s", buf);
    }
    return 0;
}

/**
 * @brief 校验并核销一个备用码
 *
 * @param[in] backup_codes_json 原始备用码 JSON 字符串
 * @param[in] input_code 用户输入的备用码
 * @param[out] out_updated_json 输出扣减后的新 JSON 字符串
 * @param[in] cap 输出缓冲区容量
 * @return bool true 成功核销，false 匹配失败
 */
bool
totp_verify_and_consume_backup_code(const char* backup_codes_json,
                                    const char* input_code,
                                    char*       out_updated_json,
                                    size_t      cap)
{
    if (!backup_codes_json || !backup_codes_json[0] || !input_code || !input_code[0]) {
        return false;
    }

    csilk_json_t* arr = csilk_json_parse(backup_codes_json);
    if (!arr || !csilk_json_is_array(arr)) {
        if (arr) {
            csilk_json_free(arr);
        }
        return false;
    }

    size_t count = csilk_json_array_size(arr);
    int    match_idx = -1;

    for (size_t i = 0; i < count; i++) {
        const char* c = csilk_json_string_value(csilk_json_array_get(arr, i));
        if (c && (strcmp(c, input_code) == 0 ||
                  (strlen(c) == 9 && strlen(input_code) == 8 && strncmp(c, input_code, 4) == 0 &&
                   strcmp(c + 5, input_code + 4) == 0))) {
            match_idx = (int)i;
            break;
        }
    }

    if (match_idx < 0) {
        csilk_json_free(arr);
        return false;
    }

    /* 构建剔除已使用码的新数组 */
    csilk_json_t* new_arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        if ((int)i != match_idx) {
            const char* c = csilk_json_string_value(csilk_json_array_get(arr, i));
            if (c) {
                csilk_json_array_append(new_arr, csilk_json_string_new(c));
            }
        }
    }
    csilk_json_free(arr);

    size_t slen = 0;
    char*  serialized = csilk_json_serialize(new_arr, &slen);
    csilk_json_free(new_arr);

    if (serialized) {
        if (out_updated_json && cap > 0) {
            strncpy(out_updated_json, serialized, cap - 1);
            out_updated_json[cap - 1] = '\0';
        }
        free(serialized);
    }

    return true;
}
