#include "services/ai/policy/audit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define MAX_AUDIT_LOGS 512

static ai_audit_record_t s_audit_logs[MAX_AUDIT_LOGS];
static size_t            s_audit_count = 0;
static pthread_mutex_t   s_audit_lock = PTHREAD_MUTEX_INITIALIZER;

static const char*
stage_str(ai_audit_stage_t stage)
{
    switch (stage) {
    case AI_AUDIT_STAGE_PROPOSAL:
        return "PROPOSAL";
    case AI_AUDIT_STAGE_CONFIRMATION:
        return "CONFIRMATION";
    case AI_AUDIT_STAGE_EXECUTION:
        return "EXECUTION";
    case AI_AUDIT_STAGE_REJECTION:
        return "REJECTION";
    default:
        return "UNKNOWN";
    }
}

void
ai_audit_log(const ai_audit_record_t* record)
{
    if (!record) {
        return;
    }

    ai_audit_record_t entry = *record;
    if (entry.timestamp == 0) {
        entry.timestamp = (int64_t)time(NULL);
    }

    /* 安全防线：确保不会记录任何可能包含 "token" 或 "secret" 的明文 */
    if (strstr(entry.result_summary, "token") || strstr(entry.result_summary, "secret") ||
        strstr(entry.result_summary, "mf_v2.")) {
        strncpy(entry.result_summary, "[REDACTED_SECRET]", sizeof(entry.result_summary) - 1);
    }

    pthread_mutex_lock(&s_audit_lock);
    if (s_audit_count < MAX_AUDIT_LOGS) {
        s_audit_logs[s_audit_count++] = entry;
    } else {
        /* 环形缓冲区推进 */
        memmove(
            &s_audit_logs[0], &s_audit_logs[1], sizeof(ai_audit_record_t) * (MAX_AUDIT_LOGS - 1));
        s_audit_logs[MAX_AUDIT_LOGS - 1] = entry;
    }
    pthread_mutex_unlock(&s_audit_lock);

    /* 格式化日志输出 */
    CSILK_LOG_I("[AI_POLICY_AUDIT] stage=%s actor=%lld session=%lld tool=%s risk=%s hash=%.16s... "
                "success=%d err=%s summary=%s",
                stage_str(entry.stage),
                (long long)entry.actor_id,
                (long long)entry.session_id,
                entry.tool,
                ai_risk_level_to_string(entry.risk),
                entry.arguments_hash,
                entry.success ? 1 : 0,
                entry.error_message,
                entry.result_summary);
}

const ai_audit_record_t*
ai_audit_get_recent(size_t* count)
{
    if (count) {
        *count = s_audit_count;
    }
    return s_audit_logs;
}

void
ai_audit_clear(void)
{
    pthread_mutex_lock(&s_audit_lock);
    s_audit_count = 0;
    pthread_mutex_unlock(&s_audit_lock);
}
