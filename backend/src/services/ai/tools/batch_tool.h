#pragma once
#include "csilk/csilk.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册批量操作相关工具 (propose_batch_actions)
 */
void ai_tool_batch_register_all(void);

/**
 * @brief 原子执行批量确认入账操作
 *
 * 1. 验证并单次原子消费 draft_token (anti-replay + HMAC tamper protection)
 * 2. 开启 db_tx_scope_t 事务保存点
 * 3. 逐项执行 daily_expense 或 transfer 并联动 ledger 余额
 * 4. 任一项失败整批原子回滚
 *
 * @param pool 数据库连接池
 * @param user_id 用户 ID
 * @param actions 动作 JSON 数组
 * @param draft_token 待校验令牌
 * @param out_data 成功时返回的响应结果 JSON (包含 executed_count 和 action_results)
 * @param out_code 业务状态码 (0 成功, 1002 参数错误/篡改, 1004 重放/权限, 500 系统错误)
 * @param err_buf 错误说明缓冲区
 * @param err_len 错误缓冲区长度
 * @return int 0 成功, < 0 失败
 */
int ai_batch_actions_execute(csilk_db_pool_t*    pool,
                             int64_t             user_id,
                             const csilk_json_t* actions,
                             const char*         draft_token,
                             csilk_json_t**      out_data,
                             int*                out_code,
                             char*               err_buf,
                             size_t              err_len);

#ifdef __cplusplus
}
#endif
