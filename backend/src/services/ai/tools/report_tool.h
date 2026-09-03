#pragma once
#include "services/ai/tools/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册所有通用工具与时间报表辅助 AI 工具
 */
void ai_tool_report_register_all(void);

/**
 * @brief 解析上传的文件内容为字符串
 */
char* ai_tool_parse_file_to_string(
    csilk_db_pool_t* pool, const char* data, size_t data_len, const char* filename, size_t max_len);

#ifdef __cplusplus
}
#endif
