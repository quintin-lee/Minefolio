#include "services/ai/model/response.h"
#include "common/db.h"
#include <stdlib.h>
#include <string.h>

ai_model_response_t*
ai_model_parse_response(const char* json_str)
{
    if (!json_str || !json_str[0]) {
        return NULL;
    }
    csilk_json_t* root = csilk_json_parse(json_str);
    if (!root) {
        return NULL;
    }

    ai_model_response_t* res = (ai_model_response_t*)calloc(1, sizeof(ai_model_response_t));
    if (!res) {
        csilk_json_free(root);
        return NULL;
    }

    csilk_json_t* usage = csilk_json_get(root, "usage");
    if (usage) {
        res->prompt_tokens = (int)db_get_num(usage, "prompt_tokens");
        res->completion_tokens = (int)db_get_num(usage, "completion_tokens");
        res->total_tokens = (int)db_get_num(usage, "total_tokens");
    }

    csilk_json_t* choices = csilk_json_get(root, "choices");
    if (choices && csilk_json_array_size(choices) > 0) {
        csilk_json_t* first_choice = csilk_json_array_get(choices, 0);
        const char*   fr = csilk_json_get_string(first_choice, "finish_reason");
        if (fr) {
            res->finish_reason = strdup(fr);
        }

        csilk_json_t* msg = csilk_json_get(first_choice, "message");
        if (msg) {
            const char* cnt = csilk_json_get_string(msg, "content");
            if (cnt) {
                res->content = strdup(cnt);
            }

            csilk_json_t* tc_arr = csilk_json_get(msg, "tool_calls");
            if (tc_arr && csilk_json_array_size(tc_arr) > 0) {
                size_t n = csilk_json_array_size(tc_arr);
                res->tool_calls = (ai_model_tool_call_t*)calloc(n, sizeof(ai_model_tool_call_t));
                res->tool_call_count = (int)n;
                for (size_t i = 0; i < n; i++) {
                    csilk_json_t* item = csilk_json_array_get(tc_arr, i);
                    const char*   tid = csilk_json_get_string(item, "id");
                    if (tid) {
                        res->tool_calls[i].id = strdup(tid);
                    }
                    csilk_json_t* fn = csilk_json_get(item, "function");
                    if (fn) {
                        const char* fn_name = csilk_json_get_string(fn, "name");
                        const char* fn_args = csilk_json_get_string(fn, "arguments");
                        if (fn_name) {
                            res->tool_calls[i].name = strdup(fn_name);
                        }
                        if (fn_args) {
                            res->tool_calls[i].arguments = strdup(fn_args);
                        }
                    }
                }
            }
        }
    }

    csilk_json_free(root);
    return res;
}

void
ai_model_response_free(ai_model_response_t* resp)
{
    if (!resp) {
        return;
    }
    if (resp->content) {
        free(resp->content);
    }
    if (resp->finish_reason) {
        free(resp->finish_reason);
    }
    if (resp->tool_calls) {
        for (int i = 0; i < resp->tool_call_count; i++) {
            if (resp->tool_calls[i].id) {
                free(resp->tool_calls[i].id);
            }
            if (resp->tool_calls[i].name) {
                free(resp->tool_calls[i].name);
            }
            if (resp->tool_calls[i].arguments) {
                free(resp->tool_calls[i].arguments);
            }
        }
        free(resp->tool_calls);
    }
    free(resp);
}
