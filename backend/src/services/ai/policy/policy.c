#include "services/ai/policy/policy.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define MAX_RATE_LIMIT_USERS 128

typedef struct {
    int64_t user_id;
    int64_t minute_window;
    int     count;
} user_rate_limit_t;

static ai_policy_rules_t s_rules = {
    .single_amount_limit = 500000.0,
    .large_amount_threshold = 50000.0,
    .max_frequency_per_minute = 60,
    .enforce_confirmation = true,
};

static user_rate_limit_t s_rate_limits[MAX_RATE_LIMIT_USERS];
static size_t            s_rate_limit_count = 0;
static pthread_mutex_t   s_policy_lock = PTHREAD_MUTEX_INITIALIZER;

ai_policy_rules_t
ai_policy_get_default_rules(void)
{
    pthread_mutex_lock(&s_policy_lock);
    ai_policy_rules_t r = s_rules;
    pthread_mutex_unlock(&s_policy_lock);
    return r;
}

void
ai_policy_set_rules(const ai_policy_rules_t* rules)
{
    if (!rules) {
        return;
    }
    pthread_mutex_lock(&s_policy_lock);
    s_rules = *rules;
    pthread_mutex_unlock(&s_policy_lock);
}

void
ai_policy_reset_frequency_limits(void)
{
    pthread_mutex_lock(&s_policy_lock);
    s_rate_limit_count = 0;
    pthread_mutex_unlock(&s_policy_lock);
}

static bool
check_and_increment_frequency_locked(int64_t user_id, int max_per_minute)
{
    if (user_id <= 0 || max_per_minute <= 0) {
        return true;
    }
    int64_t current_minute = (int64_t)time(NULL) / 60;

    for (size_t i = 0; i < s_rate_limit_count; i++) {
        if (s_rate_limits[i].user_id == user_id) {
            if (s_rate_limits[i].minute_window == current_minute) {
                if (s_rate_limits[i].count >= max_per_minute) {
                    return false; /* 触发限流 */
                }
                s_rate_limits[i].count++;
                return true;
            } else {
                /* 新的一分钟，重置计数 */
                s_rate_limits[i].minute_window = current_minute;
                s_rate_limits[i].count = 1;
                return true;
            }
        }
    }

    if (s_rate_limit_count < MAX_RATE_LIMIT_USERS) {
        s_rate_limits[s_rate_limit_count].user_id = user_id;
        s_rate_limits[s_rate_limit_count].minute_window = current_minute;
        s_rate_limits[s_rate_limit_count].count = 1;
        s_rate_limit_count++;
        return true;
    }

    return true;
}

ai_policy_decision_t*
ai_policy_evaluate(int64_t             user_id,
                   int64_t             session_id,
                   const char*         tool_name,
                   const csilk_json_t* args)
{
    (void)session_id;
    ai_policy_decision_t* dec = (ai_policy_decision_t*)calloc(1, sizeof(ai_policy_decision_t));
    if (!dec) {
        return NULL;
    }

    dec->perm_level = ai_permission_get_level(tool_name);
    dec->risk_level = ai_risk_assess(tool_name, args);

    /* 1. 认证检验 (Authentication) */
    if (user_id <= 0) {
        dec->allowed = false;
        snprintf(dec->reason,
                 sizeof(dec->reason),
                 "Authentication required: invalid or unauthenticated user");
        return dec;
    }

    /* 2. 授权权限检验 (Authorization & User Permission) */
    if (!ai_permission_check(user_id, dec->perm_level)) {
        dec->allowed = false;
        snprintf(dec->reason,
                 sizeof(dec->reason),
                 "Authorization denied: user role insufficient for %s",
                 tool_name ? tool_name : "operation");
        return dec;
    }

    /* 3. 频次限流检验 (Frequency Limit) */
    pthread_mutex_lock(&s_policy_lock);
    int    max_freq = s_rules.max_frequency_per_minute;
    double single_limit = s_rules.single_amount_limit;
    double large_threshold = s_rules.large_amount_threshold;
    bool   freq_ok = check_and_increment_frequency_locked(user_id, max_freq);
    pthread_mutex_unlock(&s_policy_lock);

    if (!freq_ok) {
        dec->allowed = false;
        snprintf(dec->reason,
                 sizeof(dec->reason),
                 "Frequency limit exceeded: maximum %d calls per minute allowed",
                 max_freq);
        return dec;
    }

    /* 4. 金额风控与动态风险升级 (Amount Limit & Risk Escalation) */
    if (args) {
        double amount = db_get_num(args, "amount");
        if (amount > 0.0) {
            if (single_limit > 0.0 && amount > single_limit) {
                dec->allowed = false;
                snprintf(dec->reason,
                         sizeof(dec->reason),
                         "Amount limit exceeded: transaction amount %.2f exceeds maximum single "
                         "limit %.2f",
                         amount,
                         single_limit);
                return dec;
            }
            if (large_threshold > 0.0 && amount >= large_threshold) {
                dec->risk_level = AI_RISK_CRITICAL;
            }
        }
    }

    dec->allowed = true;
    snprintf(dec->reason,
             sizeof(dec->reason),
             "Policy check passed, risk=%s",
             ai_risk_level_to_string(dec->risk_level));

    /* 5. 确认要求评估 (Confirmation Requirement) */
    if (s_rules.enforce_confirmation && (dec->risk_level >= AI_RISK_MEDIUM)) {
        dec->requires_confirmation = true;
    }

    return dec;
}

void
ai_policy_decision_free(ai_policy_decision_t* decision)
{
    if (!decision) {
        return;
    }
    if (decision->draft) {
        ai_confirmation_draft_free(decision->draft);
    }
    free(decision);
}
