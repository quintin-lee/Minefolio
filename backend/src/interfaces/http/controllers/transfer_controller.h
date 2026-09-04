#pragma once

#include "csilk/csilk.h"
#include "csilk/app/app.h"

void api_transfers_create(csilk_ctx_t* c);

// Backward-compatibility alias
void transfers_create(csilk_ctx_t* c);

void register_transfer_routes(csilk_app_t* app);
