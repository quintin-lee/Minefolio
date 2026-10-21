#pragma once
#include "services/ai/tools/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册网络搜索 (web_search) 与网页深度阅读 (fetch_webpage) 工具
 */
void ai_tool_web_search_register_all(void);

/**
 * @brief 解码 Bing 点击跟踪 URL（/ck/a?u=a1<base64>）还原原始目标链接
 */
char* mf_bing_unpack_url(const char* href);

/**
 * @brief 解析 Bing SERP HTML 返回条目列表
 */
csilk_json_t* mf_bing_html_parse(const char* html, size_t max_results);

#ifdef __cplusplus
}
#endif
