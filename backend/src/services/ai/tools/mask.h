#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 是否启用「金额分级脱敏」模式。
 *
 * 分级策略：
 *  - 身份标识（银行卡 / 手机号 / 身份证 / 邮箱）：始终强制脱敏（默认开启）。
 *  - 金额绝对值：默认保留；仅当环境变量 MINEFOLIO_AI_MASK_AMOUNTS=1 时
 *    将金额类数值字段量化到 1 位有效数字（数量级化），避免 LLM 经手精确余额。
 */
bool ai_mask_amounts_enabled(void);

/**
 * 对一段自由文本做身份标识 PII 脱敏（原地语义：输入输出等长，掩码保留长度）。
 *
 * 覆盖：
 *  - 银行卡号（13–19 位，允许 4 位空格/短横分隔）：保留末 4 位
 *  - 手机号（1[3-9] 开头 11 位）：保留前 3 + 末 4
 *  - 身份证号（18 位，或 17 位 + X/x）：保留前 6 + 末 4
 *  - 邮箱：local part 保留首字符，其余掩码
 *
 * @param in     输入文本（NUL 结尾）
 * @param out    输出缓冲（NUL 结尾）
 * @param out_len 输出缓冲容量（建议 >= strlen(in)+1；不足则截断）
 * 注：in 与 out 可以是同一块内存（in == out 也安全）。
 */
void ai_mask_pii_text(const char* in, char* out, size_t out_len);

/**
 * 对单个 AI 工具结果 JSON 字符串脱敏后重新序列化返回。
 *
 * - 内部工具（get_asset_detail / get_transactions / propose_* / mcp:* 等）：
 *   强制 PII 文本脱敏 + 键名黑名单（丢弃 user_id 类身份键）+（金额模式下）数值量化。
 * - 无用户数据 / 外部内容工具（get_current_time / calculate_date_range /
 *   get_exchange_rate / web_search / fetch_webpage / list_skills / use_skill）：
 *   原样返回，避免破坏 URL / 日期 / 汇率等结构。
 * - 非 JSON 的纯文本结果（如部分 MCP 工具）：退化为整体 PII 文本脱敏。
 *
 * 若脱敏未产生任何变化，返回与输入逐字节一致的副本（strdup），
 * 以便下游 JSON 比较 / 集成测试保持稳定。
 *
 * @param tool_name 工具名（mcp: 前缀亦可），NULL 视为普通工具
 * @param raw_json  工具原始结果（JSON 或纯文本），NULL 返回 NULL
 * @return 调用方负责 free 的字符串
 */
char* ai_mask_tool_result(const char* tool_name, const char* raw_json);

/**
 * 对工作流注入 LLM 的结构化财务上下文 JSON 脱敏。
 *
 * 与 ai_mask_tool_result 同一套规则（强制 PII + 可选金额量化），
 * 额外：未通过 JSON 解析时退化为整体 PII 文本脱敏。
 *
 * @param workflow_id 工作流标识（仅用于日志，当前不参与策略分派）
 * @param json        结构化上下文（可能为 "{}" / NULL）
 * @return 调用方负责 free 的字符串
 */
char* ai_mask_workflow_context(const char* workflow_id, const char* json);

#ifdef __cplusplus
}
#endif
