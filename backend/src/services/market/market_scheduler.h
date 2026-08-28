#pragma once
#include "common/db.h"

int  market_scheduler_start(csilk_db_pool_t* pool);
void market_scheduler_stop(void);
