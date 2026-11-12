#include "domain/skill/rules.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>

int mf_skill_validate_name(const char* name) {
    if (!name || name[0] == '\0') {
        return -1;
    }
    size_t len = strlen(name);
    if (len > 64) {
        return -1;
    }
    if (name[0] == '-' || name[len - 1] == '-') {
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (c == '-') {
            if (i > 0 && name[i - 1] == '-') {
                return -1; // consecutive hyphens
            }
        } else if (!islower((unsigned char)c) && !isdigit((unsigned char)c)) {
            return -1; // must be lowercase a-z or 0-9
        }
    }
    return 0;
}

int mf_skill_validate_exec_path(const char* path) {
    if (!path || path[0] == '\0') {
        return 0; // empty path allowed (pure L2 skill)
    }
    if (path[0] == '/') {
        return -1; // absolute path rejected
    }
    if (strstr(path, "..") != NULL) {
        return -1; // directory traversal rejected
    }
    if (strchr(path, '\n') != NULL || strchr(path, '\r') != NULL) {
        return -1; // newlines rejected
    }
    return 0;
}

int mf_skill_validate(const mf_skill_t* skill, char* err_buf, size_t err_size) {
    if (!skill) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "Skill cannot be NULL");
        return -1;
    }
    if (mf_skill_validate_name(skill->name) != 0) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "技能名称必须为符合规范的 kebab-case (小写字母、数字及单连字符，长度 1-64)");
        return -1;
    }
    if (skill->description[0] == '\0') {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "技能描述不能为空");
        return -1;
    }
    if (mf_skill_validate_exec_path(skill->exec_cfg.path) != 0) {
        if (err_buf && err_size > 0) snprintf(err_buf, err_size, "非法脚本路径，禁止目录穿越或绝对路径");
        return -1;
    }
    return 0;
}
