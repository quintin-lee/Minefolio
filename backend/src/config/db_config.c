#include "config/db_config.h"
#include "common/db.h"
#include <stdio.h>

int
db_config_init(csilk_db_pool_t** out_pool)
{
    return db_init(out_pool);
}

int
db_config_run_migrations(csilk_db_pool_t* pool)
{
    return db_run_migrations(pool);
}
