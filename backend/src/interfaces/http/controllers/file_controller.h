#pragma once

#include "csilk/csilk.h"
#include "csilk/app/app.h"

void api_file_upload_handler(csilk_ctx_t* c);

// Backward-compatibility alias
void file_upload_handler(csilk_ctx_t* c);

void register_file_routes(csilk_app_t* app);
