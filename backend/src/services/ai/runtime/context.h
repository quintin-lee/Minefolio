#pragma once
#include "csilk/csilk.h"
#include "common/ai_config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 构建送入大模型的最终上下文 Messages 数组（注入系统提示词与历史窗口）
 * @param cfg 全局配置
 * @param history_messages 会话历史消息数组
 * @param user_prompt 当前用户最新输入
 * @return 组装好的 messages JSON 数组（调用方负责 free）
 */
csilk_json_t* ai_context_build_messages(const ai_config_t*  cfg,
                                        const csilk_json_t* history_messages,
                                        const char*         user_prompt);

#ifdef __cplusplus
}
#endif
