#include "infrastructure/repositories/ai_settings_repo_impl.h"
#include "common/db.h"
#include <string.h>

char*
mf_ai_settings_repo_load(void* pool)
{
    csilk_json_t* r = csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                                "SELECT config_json FROM ai_settings WHERE id=1",
                                                (const char*[]){NULL});
    if (!r || csilk_json_array_size(r) == 0) {
        csilk_json_free(r);
        return NULL;
    }
    const char* json = csilk_json_get_string(csilk_json_array_get(r, 0), "config_json");
    char*       result = json ? strdup(json) : NULL;
    csilk_json_free(r);
    return result;
}

int
mf_ai_settings_repo_save(void* pool, const char* config_json)
{
    csilk_json_t* r =
        csilk_db_query_param_json((csilk_db_pool_t*)pool,
                                  "INSERT INTO ai_settings (id, config_json, updated_at) "
                                  "VALUES (1, ?, CURRENT_TIMESTAMP) "
                                  "ON CONFLICT(id) DO UPDATE SET config_json=excluded.config_json, "
                                  "updated_at=CURRENT_TIMESTAMP",
                                  (const char*[]){config_json, NULL});
    int ok = (r != NULL);
    csilk_json_free(r);
    return ok ? 0 : -1;
}
