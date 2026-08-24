#pragma once
#include "csilk/csilk.h"
#include "common/ai_config.h"

void ai_init(csilk_db_pool_t* pool);
void ai_shutdown(void);
void ai_chat_handler(csilk_ctx_t* c);
ai_config_t* ai_get_config(void);
