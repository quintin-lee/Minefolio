#pragma once

#include "csilk/csilk.h"

void api_ai_trace_list_handler(csilk_ctx_t* c);
void api_ai_trace_stats_handler(csilk_ctx_t* c);
void api_ai_trace_get_handler(csilk_ctx_t* c);

void register_ai_trace_routes(csilk_app_t* app);
