#pragma once
#include "csilk/csilk.h"
#include <stdint.h>

/** @brief Load global AI settings JSON from DB */
char* mf_ai_settings_repo_load(void* pool);

/** @brief Save global AI settings JSON to DB */
int mf_ai_settings_repo_save(void* pool, const char* config_json);
