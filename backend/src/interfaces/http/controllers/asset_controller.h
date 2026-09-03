#pragma once

#include "csilk/csilk.h"

/**
 * @brief 资产 HTTP 接口控制器 (HTTP Controller)
 * @note 严格遵循 Interfaces 规范：仅负责 HTTP 请求解析、调用 Application 用例与组装标准响应 Envelope，零业务逻辑
 */

void api_assets_list(csilk_ctx_t* c);
void api_assets_create(csilk_ctx_t* c);
void api_assets_update(csilk_ctx_t* c);
void api_assets_delete(csilk_ctx_t* c);
void api_assets_detail(csilk_ctx_t* c);
void api_assets_logs_list(csilk_ctx_t* c);
void api_assets_rebuild_single(csilk_ctx_t* c);
void api_assets_rebuild_all(csilk_ctx_t* c);

void register_asset_http_routes(csilk_app_t* app);
