#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file secret.h
 * @brief 统一 Secret Provider 与安全配置管理接口
 *
 * 提供集中化的密钥与敏感配置读取入口，杜绝业务模块散落调用 getenv。
 * 遵循多级检索提供者机制：
 * 1. 测试隔离与内存覆盖（Test Overrides）
 * 2. 外部 Secret Manager 扩展（Vault / AWS Secrets Manager / K8s Secrets）
 * 3. 环境变量提供者（Environment Provider）
 * 4. 文件系统提供者（File Provider，支持 Docker Secrets 与本地凭据文件）
 *
 * 具备生产环境弱口令及占位符主动熔断拦截能力。
 */

/**
 * @brief 外部 Secret Manager 回调函数原型
 *
 * @param[in] key 密钥标识（如 "JWT_SECRET", "AI_SECRET", "DB_PASSWORD"）
 * @param[out] out 接收密钥的字符缓冲区
 * @param[in] out_size 字符缓冲区最大字节容量
 * @return int 0 表示检索成功；-1 表示未命中或错误
 */
typedef int (*secret_manager_fn)(const char* key, char* out, size_t out_size);

/**
 * @brief 注册外部 Secret Manager 统一提供者
 *
 * @param[in] fn 回调函数指针，传 NULL 可注销当前外部提供者
 */
void config_secret_set_manager(secret_manager_fn fn);

/**
 * @brief 统一检索敏感密钥 (Secret Provider)
 *
 * 遵循以下层级依次尝试检索 Secret：
 * 1. 内存测试注入覆盖 (若处于测试模式或已设置测试覆盖)
 * 2. 外部 Secret Manager 回调 (若已注册)
 * 3. 环境变量 (自动检索 MINEFOLIO_<KEY> 与 <KEY>)
 * 4. 文件系统 Secret Provider：
 *    - 优先检查 MINEFOLIO_<KEY>_FILE 或 <KEY>_FILE 指定的文件路径；
 *    - 检查 /run/secrets/<key_lower> (Docker / K8s Secrets 标准路径)；
 *    - 检查 config/secrets/<key_lower> (本地文件凭证路径)。
 *
 * 安全检查：
 * 在生产模式（非测试模式）下，若获取到的 Secret 包含禁止的占位符
 * （如 "change-me", "default-secret", "hard-coded-secret"），
 * 自动拒绝并记录致命安全告警，防止使用弱口令启动生产服务。
 *
 * @param[in] key 密钥标识名称（如 "JWT_SECRET", "AI_SECRET", "DB_PASSWORD"）
 * @param[out] out 接收密钥的字符缓冲区。若传 NULL，则使用线程局部存储 (TLS) 静态安全缓冲区返回
 * @param[in] out_size out 缓冲区的字节容量
 * @return const char* 命中且合法则返回密钥字符串指针（out 或 TLS 指针）；未找到或非法则返回 NULL
 */
const char* config_secret_get(const char* key, char* out, size_t out_size);

/**
 * @brief 读取通用配置项（支持环境/文件/默认值），供业务模块替代裸 getenv
 *
 * @param[in] key 配置项键名（如 "CORS_ORIGIN", "ENABLE_CSRF", "PORT"）
 * @param[out] out 接收配置值的缓冲区（若为 NULL 则使用 TLS 缓冲区）
 * @param[in] out_size 缓冲区字节容量
 * @param[in] default_val 默认回退值（可为 NULL）
 * @return const char* 配置值字符串
 */
const char* config_env_get(const char* key, char* out, size_t out_size, const char* default_val);

/**
 * @brief 测试模式与环境隔离控制
 */
void config_secret_set_test_mode(bool enabled);
bool config_secret_is_test_mode(void);
void config_secret_set_test_override(const char* key, const char* value);
void config_secret_clear_test_overrides(void);

/**
 * @brief 校验指定 key 的 Secret 是否已合法配置且通过安全检查
 *
 * @param[in] key 密钥标识
 * @return bool true 已配置且有效；false 未配置或使用了禁止的弱口令
 */
bool config_secret_is_valid(const char* key);

#ifdef __cplusplus
}
#endif
