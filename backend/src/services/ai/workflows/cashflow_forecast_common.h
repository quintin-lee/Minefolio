#pragma once

#include "common/db.h"
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

void   cf_get_current_month_str(char* out, size_t sz);
double cf_get_user_avg_monthly_burn(csilk_db_pool_t* pool, int64_t user_id);
double cf_get_asset_val(const csilk_json_t* a);
int    cf_days_in_month(int y, int m);

#ifdef __cplusplus
}
#endif
