#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

int config_get_str(const char* path, const char* key, char* out, size_t out_size) {
    FILE* f = fopen(path, "r");
    if (!f) { out[0] = '\0'; return -1; }
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    /* Simple key-value parser — no JSON library needed */
    char* search = buf;
    char key_quoted[256];
    snprintf(key_quoted, sizeof(key_quoted), "\"%s\"", key);

    while ((search = strstr(search, key_quoted)) != NULL) {
        search += strlen(key_quoted);
        /* skip whitespace and colon */
        while (*search == ' ' || *search == '\t' || *search == ':' || *search == ',') search++;
        /* expect opening quote */
        if (*search != '"') { search++; continue; }
        search++; /* skip opening quote */
        /* read until closing quote (no escapes in our config) */
        size_t oi = 0;
        while (*search && *search != '"' && oi < out_size - 1) {
            out[oi++] = *search++;
        }
        out[oi] = '\0';
        return 0;
    }
    out[0] = '\0';
    return -1;
}

int config_set(const char* path, const char** kv) {
    /* Build JSON object from key-value pairs */
    size_t total = 64;
    char* json = malloc(total);
    if (!json) return -1;
    json[0] = '{'; json[1] = '\0';
    size_t len = 1;

    for (int i = 0; kv[i] && kv[i + 1]; i += 2) {
        const char* k = kv[i];
        const char* v = kv[i + 1];
        /* Escape backslashes and quotes in value */
        char escaped[1024];
        size_t ei = 0;
        for (const char* p = v; *p && ei < sizeof(escaped) - 3; p++) {
            if (*p == '\\' || *p == '"') escaped[ei++] = '\\';
            escaped[ei++] = *p;
        }
        escaped[ei] = '\0';

        size_t entry_len = (size_t)snprintf(NULL, 0, ",\"%s\":\"%s\"", k, escaped) + 1;
        if (len + entry_len >= total) {
            total = total * 2 + entry_len;
            char* tmp = realloc(json, total);
            if (!tmp) { free(json); return -1; }
            json = tmp;
        }
        len += (size_t)snprintf(json + len, total - len, ",\"%s\":\"%s\"", k, escaped);
    }
    size_t last = len > 1 ? len - 1 : 0; /* skip leading comma */
    json[last] = '}';
    json[last + 1] = '\n';
    json[last + 2] = '\0';

    /* Ensure parent dir exists */
    char dir[512];
    strncpy(dir, path, sizeof(dir) - 1); dir[sizeof(dir) - 1] = '\0';
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

    FILE* f = fopen(path, "w");
    if (!f) { free(json); return -1; }
    fprintf(f, "%s", json);
    fclose(f);
    free(json);
    return 0;
}
