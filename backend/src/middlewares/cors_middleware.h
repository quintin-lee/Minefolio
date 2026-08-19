#pragma once
#include "csilk/csilk.h"
void cors_middleware_wrapper(csilk_ctx_t* c);
void cors_preflight_handler(csilk_ctx_t* c);
