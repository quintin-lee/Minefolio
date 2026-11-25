#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t id;
    int64_t user_id;
    char    name[128];
    char    description[256];
    char    job_type[32];         /* "ai_prompt", "structured_report", "system_builtin" */
    char    trigger_type[32];     /* "cron", "market_event", "market_session" */
    char    trigger_config[512];  /* JSON */
    char    payload_config[2048]; /* JSON */
    char    delivery_channel[32]; /* "user_default", "custom_webhook", "silent_db" */
    char    delivery_config[512]; /* JSON */
    bool    enabled;
    int     is_builtin;
    char    last_run_at[32];
    char    last_run_status[16];
    int     last_run_duration_ms;
    char    next_run_at[32];
    char    created_at[32];
    char    updated_at[32];
} mf_scheduled_job_t;

typedef struct {
    int64_t id;
    int64_t user_id;
    /* "serverchan", "pushdeer", "wechat_work", "webhook", "wechat_ilink", "none" */
    char channel_type[32];
    char channel_name[64];
    char webhook_url[256];
    char secret_key[128];
    bool is_active;
    char created_at[32];
    char updated_at[32];
} mf_user_notification_channel_t;

typedef struct {
    int64_t id;
    int64_t job_id;
    int64_t user_id;
    char    started_at[32];
    char    completed_at[32];
    char    status[16];         /* "running", "success", "failed", "timeout" */
    int     duration_ms;
    char    trigger_reason[64]; /* "cron_tick", "manual_invoke", "market_closed", "price_spike" */
    char    output_summary[2048];
    char    error_message[512];
    char    notification_status[16]; /* "none", "queued", "sent", "partial", "failed" */
} mf_job_run_log_t;
