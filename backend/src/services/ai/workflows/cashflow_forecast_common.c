#include "services/ai/workflows/cashflow_forecast_common.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

void
cf_get_current_month_str(char* out, size_t sz)
{
    time_t    now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(out, sz, "%04d-%02d", tm_buf.tm_year + 1900, tm_buf.tm_mon + 1);
}

double
cf_get_user_avg_monthly_burn(csilk_db_pool_t* pool, int64_t user_id)
{
    char uid_str[32];
    snprintf(uid_str, sizeof(uid_str), "%lld", (long long)user_id);
    const char*   params[] = {uid_str, NULL};
    csilk_json_t* res =
        csilk_db_query_param_json(pool,
                                  "SELECT AVG(monthly_sum) as avg_burn FROM ("
                                  "  SELECT SUM(amount) as monthly_sum FROM daily_expenses "
                                  "  WHERE user_id = ? AND expense_type = 'expense' "
                                  "  GROUP BY substr(expense_date, 1, 7)"
                                  ")",
                                  params);
    double avg_burn = 0.0;
    if (res && csilk_json_array_size(res) > 0) {
        avg_burn = db_get_num(csilk_json_array_get(res, 0), "avg_burn");
    }
    if (res) {
        csilk_json_free(res);
    }
    return avg_burn;
}

double
cf_get_asset_val(const csilk_json_t* a)
{
    if (!a) {
        return 0.0;
    }
    double v = db_get_num(a, "current_value");
    if (v == 0.0) {
        v = db_get_num(a, "balance");
    }
    return v;
}

int
cf_days_in_month(int y, int m)
{
    static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int              d = dm[m - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) {
        d = 29;
    }
    return d;
}
