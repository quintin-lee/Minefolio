#include "services/ai/memory/memory.h"
#include <string.h>

csilk_json_t*
ai_memory_build_messages(const char*         system_prompt,
                         const csilk_json_t* history_messages,
                         const char*         user_prompt,
                         int                 max_history)
{
    csilk_json_t* messages = csilk_json_array();

    /* 1. System message */
    const char* sys = (system_prompt && system_prompt[0])
                          ? system_prompt
                          : "你是一个专业的个人财务与财富管理AI助手。";
    csilk_json_t* sys_msg = csilk_json_object();
    csilk_json_add_string(sys_msg, "role", "system");
    csilk_json_add_string(sys_msg, "content", sys);
    csilk_json_array_append(messages, sys_msg);

    /* 2. Sliding window of history messages */
    int win_size = max_history > 0 ? max_history : 20;
    if (history_messages && csilk_json_is_array(history_messages)) {
        size_t total = csilk_json_array_size(history_messages);
        size_t start = total > (size_t)win_size ? total - (size_t)win_size : 0;
        for (size_t i = start; i < total; i++) {
            csilk_json_t* item = csilk_json_array_get(history_messages, i);
            if (item) {
                csilk_json_t* m = csilk_json_object();
                const char*   role = csilk_json_get_string(item, "role");
                const char*   content = csilk_json_get_string(item, "content");
                csilk_json_add_string(m, "role", role ? role : "user");
                csilk_json_add_string(m, "content", content ? content : "");
                csilk_json_array_append(messages, m);
            }
        }
    }

    /* 3. Latest User Prompt */
    if (user_prompt && user_prompt[0]) {
        csilk_json_t* u_msg = csilk_json_object();
        csilk_json_add_string(u_msg, "role", "user");
        csilk_json_add_string(u_msg, "content", user_prompt);
        csilk_json_array_append(messages, u_msg);
    }

    return messages;
}
