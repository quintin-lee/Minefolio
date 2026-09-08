/**
 * @file rules.c
 * @brief 文件上传与导入业务规则实现 (Domain File Rules)
 */

#include "domain/file/rules.h"
#include <string.h>

bool
mf_file_rule_validate_supported(const char* filename)
{
    if (!filename || !filename[0]) {
        return false;
    }
    const char* dot = strrchr(filename, '.');
    if (!dot) {
        return false;
    }
    return strcmp(dot, ".txt") == 0 || strcmp(dot, ".log") == 0 || strcmp(dot, ".md") == 0 ||
           strcmp(dot, ".csv") == 0 || strcmp(dot, ".tsv") == 0 || strcmp(dot, ".pdf") == 0 ||
           strcmp(dot, ".zip") == 0;
}

bool
mf_file_rule_validate_size(size_t len)
{
    return len > 0 && len < 10 * 1024 * 1024; /* 10MB limit */
}

bool
mf_file_rule_validate_csv_required(const char* date,
                                   const char* asset_name,
                                   const char* type,
                                   const char* amount)
{
    return date && date[0] && asset_name && asset_name[0] && type && type[0] && amount && amount[0];
}
