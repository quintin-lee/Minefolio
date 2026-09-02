#include "services/ai/model/model.h"
#include <string.h>

const char*
ai_model_resolve_name(const ai_provider_t* provider,
                      const ai_config_t*   cfg,
                      const char*          requested_model)
{
    if (requested_model && requested_model[0]) {
        return requested_model;
    }
    if (provider && provider->model_count > 0 && provider->models && provider->models[0]) {
        return provider->models[0];
    }
    if (cfg && cfg->default_model[0]) {
        return cfg->default_model;
    }
    return "gpt-4o";
}

csilk_json_t*
ai_model_list_to_json(const ai_provider_t* provider)
{
    csilk_json_t* arr = csilk_json_array();
    if (!provider || !provider->models) {
        return arr;
    }

    for (int i = 0; i < provider->model_count; i++) {
        if (provider->models[i] && provider->models[i][0]) {
            csilk_json_array_append(arr, csilk_json_string_new(provider->models[i]));
        }
    }
    return arr;
}
