#pragma once
#include "csilk/drivers/db.h"
int db_config_init(csilk_db_pool_t** out_pool);
int db_config_run_migrations(csilk_db_pool_t* pool);
