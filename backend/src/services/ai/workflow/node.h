#pragma once
#include "services/ai/workflow/context.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char* (*ai_wf_node_fn)(csilk_db_pool_t*    pool,
                               int64_t             user_id,
                               const csilk_json_t* params,
                               const char*         context_json);

/**
 * @brief 节点执行前置判定函数
 * @param pool 数据库连接池
 * @param user_id 用户 ID
 * @param shared_state 当前工作流累计的上下文/共享状态 JSON 对象
 * @return true 允许执行, false 跳过该步骤
 */
typedef bool (*ai_wf_condition_fn)(csilk_db_pool_t*    pool,
                                   int64_t             user_id,
                                   const csilk_json_t* shared_state);

/**
 * @brief 节点异常补偿回滚函数 (Saga 逆向补偿)
 * @param pool 数据库连接池
 * @param user_id 用户 ID
 * @param shared_state 当前工作流累计的上下文/共享状态 JSON 对象
 * @return 0 补偿成功, < 0 补偿失败
 */
typedef int (*ai_wf_compensate_fn)(csilk_db_pool_t*    pool,
                                   int64_t             user_id,
                                   const csilk_json_t* shared_state);

typedef struct {
    const char*   node_id;
    const char*   title;
    const char*   description;
    ai_wf_node_fn execute;
    ai_wf_condition_fn condition; /**< 前置判定条件：若不为空且返回 false，则跳过该步骤 */
    ai_wf_compensate_fn compensate; /**< 失败补偿清理函数：工作流异常中止时逆序触发 */
    int max_retries;                /**< 瞬时重试次数，默认 0 */
} ai_workflow_node_t;

#ifdef __cplusplus
}
#endif
