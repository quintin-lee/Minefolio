#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[128];
    char description[256];
    char job_type[32];         /* "ai_prompt", "structured_report" */
    char trigger_type[32];     /* "cron", "market_event" */
    char trigger_config[512];  /* JSON */
    char payload_config[1024]; /* JSON */
    char delivery_channel[32]; /* "user_default", "custom_webhook", "silent_db" */
    char delivery_config[512]; /* JSON */
} job_create_dto_t;

typedef struct {
    char name[128];
    char description[256];
    char trigger_type[32];
    char trigger_config[512];
    char payload_config[1024];
    char delivery_channel[32];
    char delivery_config[512];
} job_update_dto_t;

typedef struct {
    char channel_type[32];
    char channel_name[64];
    char webhook_url[256];
    char secret_key[128];
} channel_save_dto_t;

#ifdef __cplusplus
}
#endif
