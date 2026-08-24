#include "common/ai_config.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void parse_string_array(const csilk_json_t *arr, char ***out_ptrs, int *out_count) {
    if (!arr || !csilk_json_is_array(arr)) { *out_ptrs = NULL; *out_count = 0; return; }
    int n = csilk_json_array_size(arr);
    *out_ptrs = (char**)malloc(sizeof(char*) * (size_t)n + 1);
    if (!*out_ptrs) { *out_count = 0; return; }
    *out_count = n;
    for (size_t i = 0; i < (size_t)n; i++) {
        const char *s = csilk_json_get_string(csilk_json_array_get(arr, i), NULL);
        (*out_ptrs)[i] = s ? strdup(s) : strdup("");
    }
    (*out_ptrs)[n] = NULL;
}

static void free_string_array(char **arr) {
    if (!arr) return;
    for (int i = 0; arr[i]; i++) free(arr[i]);
    free(arr);
}

int ai_config_load(const char *path, ai_config_t *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    csilk_json_t *root = csilk_json_parse(buf);
    if (!root) return -1;

    /* providers */
    const csilk_json_t *prov_arr = csilk_json_get(root, "providers");
    if (prov_arr && csilk_json_is_array(prov_arr)) {
        int pc = csilk_json_array_size(prov_arr);
        out->providers = (ai_provider_t*)malloc(sizeof(ai_provider_t) * (size_t)pc);
        if (!out->providers) { csilk_json_free(root); return -1; }
        out->provider_count = pc;
        for (int i = 0; i < pc; i++) {
            const csilk_json_t *p = csilk_json_array_get(prov_arr, i);
            strncpy(out->providers[i].id,       csilk_json_get_string(p, "id")       ?: "", sizeof(out->providers[i].id)   - 1);
            strncpy(out->providers[i].name,     csilk_json_get_string(p, "name")     ?: "", sizeof(out->providers[i].name)  - 1);
            strncpy(out->providers[i].api_key,  csilk_json_get_string(p, "api_key")  ?: "", sizeof(out->providers[i].api_key) - 1);
            strncpy(out->providers[i].base_url, csilk_json_get_string(p, "base_url") ?: "", sizeof(out->providers[i].base_url)- 1);
            parse_string_array(csilk_json_get(p, "models"), &out->providers[i].models, &out->providers[i].model_count);
        }
    }

    const csilk_json_t *dp = csilk_json_get(root, "default_provider");
    if (dp) strncpy(out->default_provider, csilk_json_get_string(dp, "default_provider"), sizeof(out->default_provider) - 1);
    const csilk_json_t *dm = csilk_json_get(root, "default_model");
    if (dm) strncpy(out->default_model, csilk_json_get_string(dm, "default_model"), sizeof(out->default_model) - 1);
    const csilk_json_t *cs_val = csilk_json_get(root, "context_size");
    out->context_size = cs_val ? (int)csilk_json_number_value(cs_val) : 20;
    if (out->context_size < 5) out->context_size = 20;
    const csilk_json_t *sp = csilk_json_get(root, "system_prompt");
    if (sp) strncpy(out->system_prompt, csilk_json_get_string(sp, "system_prompt"), sizeof(out->system_prompt) - 1);

    csilk_json_free(root);
    return 0;
}

int ai_config_save(const char *path, const ai_config_t *cfg) {
    size_t total = 8192;
    char *json = (char*)malloc(total);
    if (!json) return -1;
    int len = 0;

    len += snprintf(json + len, total - (size_t)len, "{\"providers\":[");
    for (int i = 0; i < cfg->provider_count; i++) {
        if (i > 0) len += snprintf(json + len, total - (size_t)len, ",");
        len += snprintf(json + len, total - (size_t)len,
            "{\"id\":\"%s\",\"name\":\"%s\",\"base_url\":\"%s\",\"models\":[",
            cfg->providers[i].id, cfg->providers[i].name,
            cfg->providers[i].base_url);
        for (int j = 0; j < cfg->providers[i].model_count; j++) {
            if (j > 0) len += snprintf(json + len, total - (size_t)len, ",");
            len += snprintf(json + len, total - (size_t)len, "\"%s\"", cfg->providers[i].models[j]);
        }
        len += snprintf(json + len, total - (size_t)len, "]}");
    }
    len += snprintf(json + len, total - (size_t)len,
        "],\"default_provider\":\"%s\",\"default_model\":\"%s\","
        "\"context_size\":%d,\"system_prompt\":\"%s\"}",
        cfg->default_provider, cfg->default_model,
        cfg->context_size, cfg->system_prompt);

    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1); dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir(dir, 0755); }

    FILE *f = fopen(path, "w");
    if (!f) { free(json); return -1; }
    fprintf(f, "%s", json);
    fclose(f);
    free(json);
    return 0;
}

void ai_config_free(ai_config_t *cfg) {
    if (cfg->providers) {
        for (int i = 0; i < cfg->provider_count; i++)
            free_string_array(cfg->providers[i].models);
        free(cfg->providers);
    }
}

ai_provider_t *ai_config_find_provider(const ai_config_t *cfg, const char *provider_id) {
    for (int i = 0; i < cfg->provider_count; i++)
        if (strcmp(cfg->providers[i].id, provider_id) == 0)
            return &cfg->providers[i];
    return NULL;
}

ai_provider_t *ai_config_default_provider(const ai_config_t *cfg) {
    return ai_config_find_provider(cfg, cfg->default_provider);
}
