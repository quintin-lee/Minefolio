#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char channel_type[32]; /* "serverchan", "pushdeer", "webhook" */
    char channel_name[64]; /* display name, e.g. "个人微信" */
    char webhook_url[256]; /* custom webhook url or serverchan endpoint */
    char secret_key[128];  /* sendkey, pushkey, or auth token */
} push_channel_config_t;

/**
 * Send WeChat / push notification via configured channel.
 * Returns 0 on success, -1 on failure with error details written to err_buf.
 */
int wechat_push_service_send(const push_channel_config_t* cfg,
                             const char*                  title,
                             const char*                  markdown_body,
                             char*                        err_buf,
                             size_t                       err_len);

#ifdef __cplusplus
}
#endif
