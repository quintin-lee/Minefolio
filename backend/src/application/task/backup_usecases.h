#pragma once

/**
 * @file backup_usecases.h
 * @brief 系统备份管理应用层用例接口
 */

#include "csilk/csilk.h"
#include <stdbool.h>
#include <stdint.h>

int backup_usecase_get_status(void* pool, const char* backup_dir, csilk_json_t** out_status);

int64_t backup_usecase_create_submit(void* pool, int64_t user_id, bool* out_deduplicated);

bool backup_usecase_is_valid_filename(const char* filename);

int backup_usecase_delete(const char* backup_dir, const char* filename);
