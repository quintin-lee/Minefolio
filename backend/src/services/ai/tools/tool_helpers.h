#pragma once
#include "services/ai/tools/context.h"
#include "services/ai/policy/risk.h"
#include "csilk/csilk.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 校验工具执行上下文（非空、含连接池、user_id > 0）
 */
bool ai_tool_ctx_valid(const ai_tool_context_t* ctx);

/**
 * @brief 构造 JSON 错误响应（可选附带业务错误码 code > 0）
 */
char* ai_tool_json_error(const char* message, int code);

/**
 * @brief 序列化 JSON 对象并释放原对象
 */
char* ai_tool_json_serialize_free(csilk_json_t* obj);

/**
 * @brief 统一解析分页参数 page / page_size
 */
void ai_tool_parse_pagination(const csilk_json_t* args,
                              int64_t             default_size,
                              int64_t             max_size,
                              int64_t*            out_page,
                              int64_t*            out_page_size);

/**
 * @brief 按优先级解析账本 ID：args.ledger_id -> ctx->metadata.ledger_id -> 可选默认账本
 */
int64_t ai_tool_resolve_ledger_id(const ai_tool_context_t* ctx,
                                  const csilk_json_t*      args,
                                  bool                     fallback_default);

/**
 * @brief 获取服务器本地当天日期字符串 YYYY-MM-DD
 */
void ai_tool_today_str(char* buf, size_t sz);

/**
 * @brief ASCII 大小写不敏感子串匹配
 */
int ai_tool_str_icontains(const char* haystack, const char* needle);

/**
 * @brief 统一签发 propose_* 草案确认令牌并构造标准卡片响应
 *
 * 将 proposed_args 中的全部规范字段和 display_extra 中的展示辅助字段合入响应，
 * 同时附带 action_type, propose_success, draft_token, draft_args_json。
 */
char* ai_tool_propose_finalize(const ai_tool_context_t* ctx,
                               const char*              action_type,
                               const char*              confirm_tool_name,
                               ai_risk_level_t          risk,
                               const csilk_json_t*      proposed_args,
                               const csilk_json_t*      display_extra);

/**
 * @brief 统一校验并单次消费 confirm_* 工具传入的 draft_token
 * @return 成功返回 NULL；失败返回堆分配的 JSON 错误字符串
 */
char* ai_tool_confirm_verify(const ai_tool_context_t* ctx,
                             const char*              confirm_tool_name,
                             const csilk_json_t*      args);

#ifdef __cplusplus
}
#endif
