#pragma once
#include "services/ai/tools/registry.h"
#include "services/ai/tools/context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通过完整安全流水线调度并执行 AI 工具
 *
 * 流水线包含：
 * 1. Tool Resolver (名称查找与存在性确认)
 * 2. Schema Validator (JSON 结构合法性、必填字段与类型校验)
 * 3. Permission Check (用户权限校验)
 * 4. Risk Check (风险等级评定与拦截)
 * 5. Confirmation (高危动账操作两步确认草案机制)
 * 6. Tool Executor (业务执行与事务原子控制)
 * 7. Audit & Trace (审计日志与执行链路追踪)
 *
 * @param ctx 工具执行上下文环境
 * @param tool_name 工具名称
 * @param raw_arguments_json 模型输出的原始 JSON 参数字符串
 * @return char* 堆分配的 JSON 字符串结果（调用方负责释放）
 */
char* ai_tool_dispatch(const ai_tool_context_t* ctx,
                       const char*              tool_name,
                       const char*              raw_arguments_json);

/**
 * @brief 已解析 JSON 参数对象的工具调度执行入口
 */
char* ai_tool_dispatch_parsed(const ai_tool_context_t* ctx,
                              const char*              tool_name,
                              const csilk_json_t*      args);

#ifdef __cplusplus
}
#endif
