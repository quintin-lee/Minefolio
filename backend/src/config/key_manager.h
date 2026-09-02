#pragma once

/**
 * @file key_manager.h
 * @brief RSA 密钥对生命周期管理与非对称加解密接口
 *
 * 管理服务端进程内生命周期的 RSA-2048 密钥对，
 * 提供公钥获取（PEM / JWK 格式）、前端登录密码解密（RSA-OAEP）等接口。
 */

#include "csilk/csilk.h"

/**
 * @brief 生成 RSA-2048 密钥对并保存在进程全局内存中
 *
 * 在服务端启动阶段调用，生成一对 RSA 密钥用于进程存续期间的前端凭证加密传输。
 * 密钥格式为 PEM 编码字符串。
 *
 * @return int 状态码
 * @retval 0 密钥对生成成功
 * @retval -1 密钥对生成失败（底层 OpenSSL / csilk 加密接口调用失败）
 *
 * @note 内存所有权：生成的密钥保存在静态缓冲区内，调用方无需手动释放。
 * @note 线程安全性：应在服务端启动时的单线程阶段调用一次，后续仅作只读访问。
 */
int auth_key_init(void);

/**
 * @brief 获取 PEM 编码的 RSA 公钥字符串
 *
 * @return const char* 指向内部静态缓冲区的 PEM 公钥字符串指针，在进程生命周期内持续有效
 *
 * @note 内存所有权：返回内部静态缓冲区指针，调用方绝对不可修改或释放该指针。
 * @note 线程安全性：只读访问，线程安全（前提为 auth_key_init 已完成）。
 */
const char* auth_key_get_public_pem(void);

/**
 * @brief 获取 PEM 编码的 RSA 私钥字符串
 *
 * @return const char* 指向内部静态缓冲区的 PEM 私钥字符串指针，在进程生命周期内持续有效
 *
 * @note 内存所有权：返回内部静态缓冲区指针，调用方绝对不可修改或释放该指针。
 * @note 线程安全性：只读访问，线程安全（前提为 auth_key_init 已完成）。
 * @note 安全警告：私钥严禁通过外部接口泄露。
 */
const char* auth_key_get_private_pem(void);

/**
 * @brief 使用服务端私钥解密 RSA-OAEP Base64URL 编码的密文字符串
 *
 * 用于解密前端通过 RSA 公钥加密传输的用户密码等敏感信息。
 * 流程包含 Base64URL 解码与 RSA-OAEP 私钥解密，解密成功后自动附加 null 终止符。
 *
 * @param[in] ciphertext_b64url Base64URL 编码的密文字符串，不可为 NULL
 * @param[out] out_buf 接收解密后明文的输出缓冲区，不可为 NULL
 * @param[in,out] out_len 输入时指定 out_buf 的容量大小；输出时写入解密明文的实际字节长度（不含终止符）
 *
 * @return int 状态码
 * @retval 0 解密成功，out_buf 包含以 '\0' 结尾的明文字符串
 * @retval -1 解密失败（参数无效、Base64URL 解码失败或非对称解密异常）
 *
 * @note 内存所有权：out_buf 由调用方分配和管理，本函数仅写入解密结果。
 * @note 线程安全性：使用服务端静态私钥进行解密计算，纯计算无状态竞争，线程安全。
 */
int auth_key_decrypt(const char* ciphertext_b64url, char* out_buf, size_t* out_len);

/**
 * @brief HTTP 控制器处理函数：处理 GET /api/auth/public-key 请求
 *
 * 将当前服务端的 RSA 公钥转换为 JWK (JSON Web Key) 格式并作为 JSON 响应返回给前端客户端，
 * 供前端使用 Web Crypto API 加密登录密码。
 *
 * @param[in,out] c csilk HTTP 上下文对象指针
 *
 * @note 响应格式：
 *   {
 *     "code": 0,
 *     "message": "ok",
 *     "data": {
 *       "public_key": {
 *         "kty": "RSA",
 *         "n": "<base64url-modulus>",
 *         "e": "<base64url-exponent>"
 *       }
 *     }
 *   }
 */
void auth_public_key(csilk_ctx_t* c);
