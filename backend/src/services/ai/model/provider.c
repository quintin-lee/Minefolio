#include "services/ai/model/provider.h"
#include <stdio.h>
#include <string.h>

const ai_provider_t*
ai_model_find_provider(const ai_config_t* cfg, const char* provider_id)
{
    if (!cfg || !cfg->providers) {
        return NULL;
    }
    const char* target = (provider_id && provider_id[0]) ? provider_id : cfg->default_provider;
    for (int i = 0; i < cfg->provider_count; i++) {
        if (strcmp(cfg->providers[i].id, target) == 0) {
            return &cfg->providers[i];
        }
    }
    return cfg->provider_count > 0 ? &cfg->providers[0] : NULL;
}

int
ai_model_build_chat_url(const ai_provider_t* provider, char* out_url, size_t sz)
{
    if (!provider || !out_url || sz == 0) {
        return -1;
    }
    const char* base = provider->base_url;
    if (!base || !base[0]) {
        base = "https://api.openai.com/v1";
    }

    size_t len = strlen(base);
    if (len > 0 && base[len - 1] == '/') {
        snprintf(out_url, sz, "%schat/completions", base);
    } else {
        snprintf(out_url, sz, "%s/chat/completions", base);
    }
    return 0;
}

int
ai_model_test_provider(const ai_provider_t* provider, const char* model, char* out_msg, size_t sz)
{
    if (!provider) {
        if (out_msg && sz > 0) {
            snprintf(out_msg, sz, "服务商未配置");
        }
        return -1;
    }
    (void)model;
    if (out_msg && sz > 0) {
        snprintf(out_msg, sz, "服务商配置就绪: %s", provider->name);
    }
    return 0;
}
