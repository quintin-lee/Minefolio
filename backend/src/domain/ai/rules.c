#include "domain/ai/rules.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int
mf_ai_rule_validate_session_title(
    const char* title, char* out_title, size_t out_cap, char* err_buf, size_t err_len)
{
    if (!out_title || out_cap == 0) {
        return -1;
    }

    if (!title || !title[0]) {
        snprintf(out_title, out_cap, "新对话");
        return 0;
    }

    size_t len = strlen(title);
    if (len > 128) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "会话标题不能超过 128 字符");
        }
        return -1;
    }

    snprintf(out_title, out_cap, "%s", title);
    return 0;
}

int
mf_ai_rule_clamp_context_size(int requested_size)
{
    if (requested_size < 5) {
        return 5;
    }
    if (requested_size > 100) {
        return 100;
    }
    return requested_size;
}

int
mf_ai_rule_calculate_token_cost(int64_t prompt_tokens,
                                int64_t completion_tokens,
                                double  prompt_unit_price,
                                double  completion_unit_price,
                                double* out_cost)
{
    if (!out_cost) {
        return -1;
    }
    if (prompt_tokens < 0 || completion_tokens < 0 || prompt_unit_price < 0 ||
        completion_unit_price < 0) {
        return -1;
    }

    double cost =
        (prompt_tokens * prompt_unit_price + completion_tokens * completion_unit_price) / 1000000.0;
    *out_cost = cost;
    return 0;
}

bool
mf_ai_rule_is_safe_provider(const char* provider_id)
{
    if (!provider_id || !provider_id[0]) {
        return false;
    }
    size_t len = strlen(provider_id);
    if (len > 32) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char ch = provider_id[i];
        if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') {
            return false;
        }
    }
    return true;
}
