#pragma once

/**
 * @file totp.h
 * @brief 基于时间的一次性密码 (TOTP, RFC 6238) 与双因子认证 (2FA) 工具接口
 *
 * 提供 Base32 密钥生成、TOTP 6 位动态验证码计算、时间窗口容错验证（±30 秒）、
 * 以及一次性备份代码（Backup Codes）的生成与核销校验功能。
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief 生成随机的 160-bit (20 字节) Base32 编码 TOTP 密钥字符串
 *
 * 使用加密安全随机数源（OpenSSL RAND_bytes 或 /dev/urandom）生成 20 字节随机数，
 * 并将其编码为 32 位字符长度的 Base32 密钥字符串（末尾附加 '\0'）。
 *
 * @param[out] out_secret 接收生成的 Base32 密钥的字符输出缓冲区
 * @param[in] cap 输出缓冲区容量（至少需 33 字节以容纳 32 字符 + 终止符）
 *
 * @return int 状态码
 * @retval 0 成功生成密钥
 * @retval -1 生成失败（参数无效、缓冲区过小或熵源读取失败）
 *
 * @note 内存所有权：缓冲区由调用方提供。
 * @note 线程安全性：线程安全。
 */
int totp_generate_secret(char* out_secret, size_t cap);

/**
 * @brief 根据给定的时间戳与 Base32 密钥计算 6 位 TOTP 数字验证码
 *
 * 遵循 RFC 6238 / RFC 4226 规范：
 * 1. 时间步长为 30 秒（step = timestamp / 30）。
 * 2. 使用 HMAC-SHA1 算法计算哈希值。
 * 3. 提取动态截断代码并取模 1,000,000 生成 6 位纯数字字符串。
 *
 * @param[in] base32_secret Base32 格式的密钥字符串，不可为 NULL
 * @param[in] timestamp Unix 时间戳（秒）
 * @param[out] out_code 接收 6 位验证码的字符输出缓冲区
 * @param[in] cap 输出缓冲区容量（至少需 7 字节以容纳 6 字符 + 终止符）
 *
 * @return int 状态码
 * @retval 0 成功计算验证码
 * @retval -1 计算失败（Base32 解码失败、HMAC 异常或参数无效）
 *
 * @note 线程安全性：纯密码学计算，线程安全。
 */
int totp_generate_code(const char* base32_secret, uint64_t timestamp, char* out_code, size_t cap);

/**
 * @brief 校验用户输入的 6 位 TOTP 动态码是否有效
 *
 * 允许当前时间步长以及前后各 1 个步长（即当前时刻 ±30 秒容差窗口），以兼容客户端与服务端时钟漂移。
 *
 * @param[in] base32_secret 用户的 Base32 密钥字符串
 * @param[in] code 用户提交的 6 位数字验证码字符串
 *
 * @return bool 校验结果
 * @retval true 验证码在容差时间窗口内匹配成功
 * @retval false 验证码错误、密钥非法或超时
 *
 * @note 线程安全性：线程安全。
 */
bool totp_verify_code(const char* base32_secret, const char* code);

/**
 * @brief 生成 8 组一次性安全备份代码 (Backup Codes)
 *
 * 每组备份码格式为 "xxxx-xxxx"（形如 "1a2b-3c4d"，共 9 个字符加 '\0'）。
 *
 * @param[out] out_codes 接收 8 组备份码的二维字符数组，每个元素大小为 16 字节
 *
 * @return int 状态码
 * @retval 0 成功生成
 * @retval -1 生成失败
 *
 * @note 内存所有权：数组内存由调用方分配。
 * @note 线程安全性：线程安全。
 */
int totp_generate_backup_codes(char out_codes[8][16]);

/**
 * @brief 验证并核销（一次性消耗）指定的备用代码
 *
 * 解析存储在数据库中的 JSON 数组字符串（如 `["1a2b-3c4d", "e5f6-7a8b"]`），
 * 匹配输入的备用码（兼容带连字符与不带连字符两种格式）；
 * 若匹配成功，则将该码从数组中移除，并将剩余备份码重新序列化为 JSON 写入 out_updated_json。
 *
 * @param[in] backup_codes_json 存储于用户表中的 JSON 数组格式备份码字符串
 * @param[in] input_code 用户输入的备用码字符串（如 "1a2b-3c4d" 或 "1a2b3c4d"）
 * @param[out] out_updated_json 接收移除已消耗备用码后的新 JSON 数组字符串缓冲区
 * @param[in] cap out_updated_json 缓冲区的容量大小
 *
 * @return bool 校验与核销结果
 * @retval true 备用码匹配成功并已更新剩余列表
 * @retval false 备用码不存在、JSON 解析失败或已全部用尽
 *
 * @note 内存所有权：out_updated_json 由调用方分配管理。
 * @note 线程安全性：纯数据解析与序列化，线程安全。
 */
bool totp_verify_and_consume_backup_code(const char* backup_codes_json,
                                         const char* input_code,
                                         char*       out_updated_json,
                                         size_t      cap);
