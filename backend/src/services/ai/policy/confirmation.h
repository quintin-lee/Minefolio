#pragma once
#include "csilk/csilk.h"
#include "services/ai/policy/risk.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum ai_confirmation_status_t
 * @brief 确认令牌校验结果状态码
 */
typedef enum {
    AI_CONFIRM_OK = 0,                 /**< 校验成功并已原子消费 (Single-use consumed) */
    AI_CONFIRM_ERR_FORMAT = -1,        /**< 令牌格式非法或解析错误 */
    AI_CONFIRM_ERR_EXPIRED = -2,       /**< 令牌已过期 (Expired) */
    AI_CONFIRM_ERR_USER_MISMATCH = -3, /**< 用户身份不匹配 (Modified User / 越权) */
    AI_CONFIRM_ERR_TOOL_MISMATCH = -4, /**< 工具名称不匹配 (Modified Tool) */
    AI_CONFIRM_ERR_ARGS_MISMATCH = -5, /**< 确认参数与拟录入参数不匹配 (Modified Argument / 篡改) */
    AI_CONFIRM_ERR_REPLAY = -6,        /**< 令牌已被使用或检测到重放 (Replay / Double Execution) */
    AI_CONFIRM_ERR_SIGNATURE = -7 /**< 签名校验失败或密钥错误 (Forged Signature / Wrong Secret) */
} ai_confirmation_status_t;

/**
 * @struct ai_confirmation_params_t
 * @brief 确认令牌绑定的完整上下文信息
 */
typedef struct {
    int64_t         user_id;    /**< 授权用户 ID */
    int64_t         session_id; /**< 会话/会话流 ID */
    const char*     tool_name;  /**< 授权的目标工具名称 */
    const char*     args_hash;  /**< 拟录入参数 SHA-256 哈希值 */
    ai_risk_level_t risk;       /**< 评定风险等级 */
    int64_t         timestamp;  /**< 创建时间戳 */
    int64_t         expiration; /**< 过期截止时间戳 */
    const char*     nonce;      /**< 唯一随机 Nonce 防重放标示 */
} ai_confirmation_params_t;

typedef struct {
    char        draft_id[64];
    char        action_name[64];
    char*       summary;
    char*       payload_json;
    bool        requires_confirmation;
    const char* warning_message;
    char        draft_token[512];
} ai_confirmation_draft_t;

/**
 * @brief 动态设置或重载 AI Policy 签名密钥（用于运行时配置注入或测试）
 */
void ai_confirmation_set_secret(const char* secret);

/**
 * @brief 获取当前生效的签名密钥（优先读取环境变量 MINEFOLIO_AI_SECRET / MINEFOLIO_JWT_SECRET，再查配置）
 */
const char* ai_confirmation_get_secret(void);

/**
 * @brief 计算 JSON 参数对象的规范化 SHA-256 哈希（排除临时 token 字段并排序键）
 *
 * @param args 参数 JSON 对象
 * @param out_hash 输出 64 字节十六进制哈希字符串缓冲区（至少 65 字节容量）
 * @param out_hash_sz 缓冲区大小
 */
void ai_confirmation_hash_args(const csilk_json_t* args, char* out_hash, size_t out_hash_sz);

/**
 * @brief 生成绑定全要素的防篡改防重放确认 Token
 *
 * 绑定要素包括：user_id, session_id, tool, arguments hash, risk, timestamp, nonce, expiration。
 *
 * @param user_id 用户 ID
 * @param session_id 会话 ID
 * @param tool_name 目标执行工具名称
 * @param args 拟录入的参数对象
 * @param risk 风险等级
 * @param ttl_seconds 有效期（秒，默认 300）
 * @return 堆分配的 Token 字符串（调用方负责 free）
 */
char* ai_confirmation_create_bound_token(int64_t             user_id,
                                         int64_t             session_id,
                                         const char*         tool_name,
                                         const csilk_json_t* args,
                                         ai_risk_level_t     risk,
                                         int                 ttl_seconds);

/**
 * @brief 校验并单次原子消费确认 Token (Single-use & Anti-replay)
 *
 * 执行恒定时间 (constant-time) HMAC 签名比对，并对 user、tool、args_hash、expiration、nonce 执行全方位校验。
 * 校验通过后立即将 Nonce 标记为已消费，任何二次使用或重放均立即拦截。
 *
 * @param user_id 当前调用用户 ID
 * @param session_id 当前会话 ID
 * @param tool_name 当前执行的工具名称
 * @param args 执行时提交的参数对象
 * @param token 客户端回传的 draft_token
 * @return 状态码，成功返回 AI_CONFIRM_OK (0)，失败返回对应的负数状态码
 */
ai_confirmation_status_t ai_confirmation_verify_and_consume(int64_t             user_id,
                                                            int64_t             session_id,
                                                            const char*         tool_name,
                                                            const csilk_json_t* args,
                                                            const char*         token);

/**
 * @brief 转换确认状态码为用户可读的错误说明
 */
const char* ai_confirmation_strerror(ai_confirmation_status_t status);

/**
 * @brief 恒定时间内存比较函数（防侧信道时序攻击）
 *
 * @param a 内存块 A
 * @param b 内存块 B
 * @param len 比较长度
 * @return 0 为完全相同，非 0 为不相同
 */
int ai_confirmation_constant_time_memcmp(const void* a, const void* b, size_t len);

/**
 * @brief 生成需要用户确认的执行草案
 */
ai_confirmation_draft_t* ai_confirmation_draft_create(const char* action_name,
                                                      const char* summary,
                                                      const char* payload_json,
                                                      const char* warning);

/**
 * @brief 释放确认草案内存
 */
void ai_confirmation_draft_free(ai_confirmation_draft_t* draft);

/**
 * @brief 将草案序列化为标准 JSON 字符串
 */
char* ai_confirmation_draft_to_json(const ai_confirmation_draft_t* draft);

/**
 * @brief 兼容接口：创建确认 Token
 */
char* ai_confirmation_create_token(int64_t user_id, double amount, const char* a, const char* b);

/**
 * @brief 兼容接口：校验确认 Token
 */
int ai_confirmation_verify_token(
    int64_t user_id, double amount, const char* a, const char* b, const char* token);

#ifdef __cplusplus
}
#endif
