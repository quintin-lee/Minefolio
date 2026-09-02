/**
 * @file config.c
 * @brief 轻量级 JSON 配置文件读写工具实现
 *
 * 实现了无需解析全量 DOM 树的高效键值对流式匹配读取，
 * 以及使用 csilk JSON API 实现的多键值持久化写入。
 */

#include "config.h"
#include "csilk/csilk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * @brief 从 JSON 文件中读取指定键名对应的字符串值
 *
 * @param[in] path 配置文件路径
 * @param[in] key 配置项键名
 * @param[out] out 接收输出字符串的缓冲区
 * @param[in] out_size 缓冲区大小
 *
 * @return int 0 成功，-1 失败
 */
int
config_get_str(const char* path, const char* key, char* out, size_t out_size)
{
    FILE* f = fopen(path, "r");
    if (!f) {
        out[0] = '\0';
        return -1;
    }
    char   buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    /* 简单的键值匹配器 — 适用于扁平简易配置 */
    char* search = buf;
    char  key_quoted[256];
    snprintf(key_quoted, sizeof(key_quoted), "\"%s\"", key);

    while ((search = strstr(search, key_quoted)) != NULL) {
        search += strlen(key_quoted);
        /* 跳过空白字符与冒号 */
        while (*search == ' ' || *search == '\t' || *search == ':' || *search == ',') {
            search++;
        }
        /* 预期开头引号 */
        if (*search != '"') {
            search++;
            continue;
        }
        search++; /* 跳过起始双引号 */
        /* 读取至闭合双引号 */
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

/**
 * @brief 将平铺键值对写入指定的 JSON 文件
 *
 * @param[in] path 目标文件路径
 * @param[in] kv NULL 结尾的键值对数组 [k1, v1, k2, v2, ..., NULL]
 *
 * @return int 0 成功，-1 失败
 */
int
config_set(const char* path, const char** kv)
{
    if (!path || !kv) {
        return -1;
    }

    csilk_json_t* obj = csilk_json_object();
    if (!obj) {
        return -1;
    }

    for (int i = 0; kv[i] && kv[i + 1]; i += 2) {
        csilk_json_add_string(obj, kv[i], kv[i + 1]);
    }

    size_t len = 0;
    char*  json = csilk_json_serialize(obj, &len);
    csilk_json_free(obj);
    if (!json) {
        return -1;
    }

    /* 确保父级目录存在 */
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
    fprintf(f, "%s\n", json);
    fclose(f);
    free(json);
    return 0;
}
