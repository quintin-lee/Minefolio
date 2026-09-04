#pragma once

#include "csilk/csilk.h"
#include "csilk/app/app.h"

void api_receipt_scan_handler(csilk_ctx_t* c);

// Backward-compatibility aliases
void receipt_scan_handler(csilk_ctx_t* c);
void receipt_service_scan(csilk_ctx_t* c);

void register_receipt_routes(csilk_app_t* app);
