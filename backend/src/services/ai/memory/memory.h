#pragma once
#include "csilk/csilk.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 构建送入大模型的受控上下文消息数组
 *
 * 保证：
 * 1. 必定包含 system prompt（居首）；
 * 2. 对历史消息按 max_history 进行尾部滑动窗口截取；
 * 3. 追加当前 user_prompt（若非空）。
 *
 * @param system_prompt 系统设定提示词
 * @param history_messages 会话历史消息数组
 * @param user_prompt 当前轮次用户输入（可为空）
 * @param max_history 最大保留历史条数
 * @return 组装好的 messages JSON 数组（需调用方 csilk_json_free 释放）
 */
csilk_json_t* ai_memory_build_messages(const char*         system_prompt,
                                       const csilk_json_t* history_messages,
                                       const char*         user_prompt,
                                       int                 max_history);

#ifdef __cplusplus
}
#endif
