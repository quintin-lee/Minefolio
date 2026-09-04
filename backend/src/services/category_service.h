#pragma once

#include "csilk/csilk.h"
#include "csilk/drivers/db.h"

/**
 * @file category_service.h
 * @brief 默认分类播种与分类领域服务
 */

void categories_seed_defaults(csilk_db_pool_t* pool, int64_t user_id);
