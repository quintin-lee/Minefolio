#include "common/ai_config.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void
parse_string_array(const csilk_json_t* arr, char*** out_ptrs, int* out_count)
{
    if (!arr || !csilk_json_is_array(arr)) {
        *out_ptrs = NULL;
        *out_count = 0;
        return;
    }
    int n = csilk_json_array_size(arr);
    *out_ptrs = (char**)malloc(sizeof(char*) * (size_t)(n + 1));
    if (!*out_ptrs) {
        *out_count = 0;
        return;
    }
    *out_count = n;
    for (size_t i = 0; i < (size_t)n; i++) {
        const char* s = csilk_json_string_value(csilk_json_array_get(arr, i));
        (*out_ptrs)[i] = s ? strdup(s) : strdup("");
    }
    (*out_ptrs)[n] = NULL;
}

static void
free_string_array(char** arr)
{
    if (!arr) {
        return;
    }
    for (int i = 0; arr[i]; i++) {
        free(arr[i]);
    }
    free(arr);
}

static int
ai_config_parse_root(const csilk_json_t* root, ai_config_t* out)
{
    const csilk_json_t* prov_arr = csilk_json_get(root, "providers");
    if (prov_arr && csilk_json_is_array(prov_arr)) {
        int pc = csilk_json_array_size(prov_arr);
        out->providers = (ai_provider_t*)malloc(sizeof(ai_provider_t) * (size_t)pc);
        if (!out->providers) {
            return -1;
        }
        out->provider_count = pc;
        for (int i = 0; i < pc; i++) {
            const csilk_json_t* p = csilk_json_array_get(prov_arr, i);
            strncpy(out->providers[i].id,
                    csilk_json_get_string(p, "id") ?: "",
                    sizeof(out->providers[i].id) - 1);
            strncpy(out->providers[i].name,
                    csilk_json_get_string(p, "name") ?: "",
                    sizeof(out->providers[i].name) - 1);
            strncpy(out->providers[i].api_key,
                    csilk_json_get_string(p, "api_key") ?: "",
                    sizeof(out->providers[i].api_key) - 1);
            strncpy(out->providers[i].base_url,
                    csilk_json_get_string(p, "base_url") ?: "",
                    sizeof(out->providers[i].base_url) - 1);
            parse_string_array(csilk_json_get(p, "models"),
                               &out->providers[i].models,
                               &out->providers[i].model_count);
        }
    }

    const char* dp = csilk_json_get_string(root, "default_provider");
    if (dp) {
        strncpy(out->default_provider, dp, sizeof(out->default_provider) - 1);
    }
    const char* dm = csilk_json_get_string(root, "default_model");
    if (dm) {
        strncpy(out->default_model, dm, sizeof(out->default_model) - 1);
    }
    const csilk_json_t* cs_val = csilk_json_get(root, "context_size");
    out->context_size = cs_val ? (int)csilk_json_number_value(cs_val) : 20;
    if (out->context_size < 5) {
        out->context_size = 20;
    }
    const char* sp = csilk_json_get_string(root, "system_prompt");
    if (sp) {
        strncpy(out->system_prompt, sp, sizeof(out->system_prompt) - 1);
    }
    return 0;
}

int
ai_config_load_json(const char* json, ai_config_t* out)
{
    memset(out, 0, sizeof(*out));
    if (!json || !json[0]) {
        return -1;
    }
    csilk_json_t* root = csilk_json_parse(json);
    if (!root) {
        return -1;
    }
    int rc = ai_config_parse_root(root, out);
    csilk_json_free(root);
    return rc;
}

int
ai_config_load(const char* path, ai_config_t* out)
{
    memset(out, 0, sizeof(*out));
    FILE* f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    char   buf[65536];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return ai_config_load_json(buf, out);
}

int
ai_config_save(const char* path, const ai_config_t* cfg)
{
    csilk_json_t* root = csilk_json_object();
    if (!root) {
        return -1;
    }

    csilk_json_t* prov_arr = csilk_json_array();
    for (int i = 0; i < cfg->provider_count; i++) {
        csilk_json_t* p = csilk_json_object();
        csilk_json_add_string(p, "id", cfg->providers[i].id);
        csilk_json_add_string(p, "name", cfg->providers[i].name);
        csilk_json_add_string(p, "api_key", cfg->providers[i].api_key);
        csilk_json_add_string(p, "base_url", cfg->providers[i].base_url);
        csilk_json_t* ml = csilk_json_array();
        for (int j = 0; j < cfg->providers[i].model_count; j++) {
            csilk_json_array_append(ml, csilk_json_string_new(cfg->providers[i].models[j]));
        }
        csilk_json_add_array(p, "models", ml);
        csilk_json_array_append(prov_arr, p);
    }
    csilk_json_add_array(root, "providers", prov_arr);
    csilk_json_add_string(root, "default_provider", cfg->default_provider);
    csilk_json_add_string(root, "default_model", cfg->default_model);
    csilk_json_add_number(root, "context_size", (double)cfg->context_size);
    csilk_json_add_string(root, "system_prompt", cfg->system_prompt);

    size_t slen = 0;
    char*  json = csilk_json_serialize(root, &slen);
    csilk_json_free(root);
    if (!json) {
        return -1;
    }

    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

    FILE* f = fopen(path, "w");
    if (!f) {
        free(json);
        return -1;
    }
    fwrite(json, 1, slen, f);
    fclose(f);
    free(json);
    return 0;
}

void
ai_config_free(ai_config_t* cfg)
{
    if (cfg->providers) {
        for (int i = 0; i < cfg->provider_count; i++) {
            free_string_array(cfg->providers[i].models);
        }
        free(cfg->providers);
    }
}

ai_provider_t*
ai_config_find_provider(const ai_config_t* cfg, const char* provider_id)
{
    for (int i = 0; i < cfg->provider_count; i++) {
        if (strcmp(cfg->providers[i].id, provider_id) == 0) {
            return &cfg->providers[i];
        }
    }
    return NULL;
}

ai_provider_t*
ai_config_default_provider(const ai_config_t* cfg)
{
    return ai_config_find_provider(cfg, cfg->default_provider);
}
