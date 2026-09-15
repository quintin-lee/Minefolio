#pragma once

#include "csilk/csilk.h"
#include "csilk/app/app.h"

void api_ai_mcp_servers_list(csilk_ctx_t* c);
void api_ai_mcp_servers_get(csilk_ctx_t* c);
void api_ai_mcp_servers_create(csilk_ctx_t* c);
void api_ai_mcp_servers_update(csilk_ctx_t* c);
void api_ai_mcp_servers_delete(csilk_ctx_t* c);
void api_ai_mcp_servers_tools(csilk_ctx_t* c);
void api_ai_mcp_servers_test(csilk_ctx_t* c);
void api_ai_mcp_servers_refresh(csilk_ctx_t* c);

void register_ai_mcp_routes(csilk_app_t* app);
