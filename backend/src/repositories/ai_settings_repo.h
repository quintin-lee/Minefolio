#pragma once
#include "csilk/csilk.h"
#include "common/db.h"

char* ai_settings_load(csilk_db_pool_t* pool);

int ai_settings_save(csilk_db_pool_t* pool, const char* config_json);
