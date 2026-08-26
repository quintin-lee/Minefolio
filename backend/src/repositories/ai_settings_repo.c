#include "repositories/ai_settings_repo.h"
#include <stdio.h>
#include <string.h>

char* ai_settings_load(csilk_db_pool_t* pool) {
    csilk_json_t* r = csilk_db_query_param_json(pool,
        "SELECT config_json FROM ai_settings WHERE id=1",
        (const char*[]){NULL});
    if (!r || csilk_json_array_size(r) == 0) { csilk_json_free(r); return NULL; }
    const char* json = csilk_json_get_string(csilk_json_array_get(r, 0), "config_json");
    char* result = json ? strdup(json) : NULL;
    csilk_json_free(r);
    return result;
}

int ai_settings_save(csilk_db_pool_t* pool, const char* config_json) {
    csilk_json_t* r = csilk_db_query_param_json(pool,
        "INSERT INTO ai_settings (id, config_json, updated_at) "
        "VALUES (1, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(id) DO UPDATE SET config_json=excluded.config_json, "
        "updated_at=CURRENT_TIMESTAMP",
        (const char*[]){config_json, NULL});
    int ok = (r != NULL);
    csilk_json_free(r);
    return ok ? 0 : -1;
}
