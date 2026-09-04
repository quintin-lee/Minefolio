#include "services/market/market_scheduler.h"
#include "application/market/usecases.h"
#include "repositories/dca_repo.h"
#include "csilk/csilk.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_t        g_sched_thread;
static pthread_mutex_t  g_sched_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_sched_cond = PTHREAD_COND_INITIALIZER;
static bool             g_sched_running = false;
static csilk_db_pool_t* g_pool = NULL;

static bool g_config_auto_sync = true;
static int  g_config_interval_min = 30;
static char g_config_sync_mode[32] = "trading_hours";

void
market_scheduler_set_config(bool auto_sync, int interval_min, const char* sync_mode)
{
    pthread_mutex_lock(&g_sched_mutex);
    g_config_auto_sync = auto_sync;
    if (interval_min >= 5 && interval_min <= 1440) {
        g_config_interval_min = interval_min;
    }
    if (sync_mode && sync_mode[0]) {
        strncpy(g_config_sync_mode, sync_mode, sizeof(g_config_sync_mode) - 1);
        g_config_sync_mode[sizeof(g_config_sync_mode) - 1] = '\0';
    }
    pthread_mutex_unlock(&g_sched_mutex);
}

void
market_scheduler_get_config(bool*  out_auto_sync,
                            int*   out_interval_min,
                            char*  out_sync_mode,
                            size_t mode_cap)
{
    pthread_mutex_lock(&g_sched_mutex);
    if (out_auto_sync) {
        *out_auto_sync = g_config_auto_sync;
    }
    if (out_interval_min) {
        *out_interval_min = g_config_interval_min;
    }
    if (out_sync_mode && mode_cap > 0) {
        strncpy(out_sync_mode, g_config_sync_mode, mode_cap - 1);
        out_sync_mode[mode_cap - 1] = '\0';
    }
    pthread_mutex_unlock(&g_sched_mutex);
}

static void
check_and_trigger_dca_plans(csilk_db_pool_t* pool, struct tm* tm_now)
{
    if (!pool) {
        return;
    }
    csilk_json_t* plans = dca_plan_list_all_active(pool);
    if (!plans) {
        return;
    }

    int wday = tm_now->tm_wday == 0 ? 7 : tm_now->tm_wday; /* 1-7 */
    int mday = tm_now->tm_mday;                            /* 1-31 */

    char today_str[32];
    snprintf(today_str,
             sizeof(today_str),
             "%04d-%02d-%02d",
             tm_now->tm_year + 1900,
             tm_now->tm_mon + 1,
             tm_now->tm_mday);

    size_t count = csilk_json_array_size(plans);
    for (size_t i = 0; i < count; ++i) {
        csilk_json_t* plan = csilk_json_array_get(plans, i);
        int64_t       plan_id = (int64_t)db_get_int(plan, "id");
        int64_t       user_id = (int64_t)db_get_int(plan, "user_id");
        const char*   freq = csilk_json_get_string(plan, "frequency");
        int           dop = (int)db_get_int(plan, "day_of_period");
        double        amount = db_get_num(plan, "amount");

        if (!freq) {
            freq = "monthly";
        }
        bool is_due_today = false;

        if (strcmp(freq, "weekly") == 0) {
            if (dop == wday) {
                is_due_today = true;
            }
        } else if (strcmp(freq, "monthly") == 0) {
            if (dop == mday) {
                is_due_today = true;
            }
        }

        if (is_due_today) {
            int64_t new_exec_id = dca_execution_create(pool, plan_id, user_id, today_str, amount);
            if (new_exec_id > 0) {
                CSILK_LOG_I(
                    "DCA scheduler generated pending execution %lld for plan %lld (date: %s)",
                    (long long)new_exec_id,
                    (long long)plan_id,
                    today_str);
            }
        }
    }
    csilk_json_free(plans);
}

static bool
is_cn_trading_hour(struct tm* tm_now)
{
    /* Monday (1) to Friday (5) */
    if (tm_now->tm_wday < 1 || tm_now->tm_wday > 5) {
        return false;
    }

    int minute_of_day = tm_now->tm_hour * 60 + tm_now->tm_min;

    /* Morning session: 09:30 - 11:30 (570 - 690) */
    if (minute_of_day >= 570 && minute_of_day <= 690) {
        return true;
    }

    /* Afternoon session: 13:00 - 15:05 (780 - 905) */
    if (minute_of_day >= 780 && minute_of_day <= 905) {
        return true;
    }

    return false;
}

static bool
is_us_trading_hour(struct tm* tm_now)
{
    /* US session in Beijing Time (21:30 - 04:00) */
    if (tm_now->tm_wday >= 1 && tm_now->tm_wday <= 5) {
        if (tm_now->tm_hour >= 21 && (tm_now->tm_hour > 21 || tm_now->tm_min >= 30)) {
            return true;
        }
    }
    if (tm_now->tm_wday >= 2 && tm_now->tm_wday <= 6) {
        if (tm_now->tm_hour < 4) {
            return true;
        }
    }
    return false;
}

static bool
is_nightly_settlement(struct tm* tm_now)
{
    /* Monday to Friday 21:30 - 21:35 for mutual fund NAVs */
    if (tm_now->tm_wday >= 1 && tm_now->tm_wday <= 5) {
        if (tm_now->tm_hour == 21 && tm_now->tm_min >= 30 && tm_now->tm_min < 35) {
            return true;
        }
    }
    return false;
}

static void*
scheduler_loop(void* arg)
{
    (void)arg;
    CSILK_LOG_I("Market quote background scheduler started");

    time_t last_sync_time = 0;
    time_t last_nightly_time = 0;
    int    last_dca_check_day = -1;

    pthread_mutex_lock(&g_sched_mutex);
    while (g_sched_running) {
        time_t    now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);

        bool should_sync = false;

        /* Check DCA plans once per day */
        if (g_pool && tm_now.tm_yday != last_dca_check_day) {
            last_dca_check_day = tm_now.tm_yday;
            pthread_mutex_unlock(&g_sched_mutex);
            check_and_trigger_dca_plans(g_pool, &tm_now);
            pthread_mutex_lock(&g_sched_mutex);
        }

        if (g_config_auto_sync && strcmp(g_config_sync_mode, "manual") != 0) {
            if (strcmp(g_config_sync_mode, "interval") == 0) {
                int interval_sec = g_config_interval_min > 0 ? (g_config_interval_min * 60) : 1800;
                if (now - last_sync_time >= interval_sec) {
                    should_sync = true;
                    last_sync_time = now;
                }
            } else {
                /* Default "trading_hours" smart mode */
                int interval_sec = g_config_interval_min > 0 ? (g_config_interval_min * 60) : 900;
                if (interval_sec < 300) {
                    interval_sec = 300;
                }

                /* Active during CN or US market sessions */
                if ((is_cn_trading_hour(&tm_now) || is_us_trading_hour(&tm_now)) &&
                    (now - last_sync_time >= interval_sec)) {
                    should_sync = true;
                    last_sync_time = now;
                }

                /* Nightly mutual fund settlement */
                if (is_nightly_settlement(&tm_now) && (now - last_nightly_time >= 3600)) {
                    should_sync = true;
                    last_nightly_time = now;
                    CSILK_LOG_I("Market scheduler triggering nightly fund settlement sync");
                }
            }
        }

        if (should_sync && g_pool) {
            pthread_mutex_unlock(&g_sched_mutex);
            int synced = 0, failed = 0;
            market_usecase_do_sync_user(g_pool, 0, &synced, &failed);
            CSILK_LOG_I("Market scheduler completed sync: %d synced, %d failed", synced, failed);
            pthread_mutex_lock(&g_sched_mutex);
        }

        /* Wait 30 seconds or until notified */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 30;
        pthread_cond_timedwait(&g_sched_cond, &g_sched_mutex, &ts);
    }
    pthread_mutex_unlock(&g_sched_mutex);

    CSILK_LOG_I("Market quote background scheduler stopped");
    return NULL;
}

int
market_scheduler_start(csilk_db_pool_t* pool)
{
    if (g_sched_running) {
        return 0;
    }

    g_pool = pool;
    g_sched_running = true;

    if (pthread_create(&g_sched_thread, NULL, scheduler_loop, NULL) != 0) {
        CSILK_LOG_E("Failed to create market scheduler thread");
        g_sched_running = false;
        return -1;
    }

    return 0;
}

void
market_scheduler_stop(void)
{
    if (!g_sched_running) {
        return;
    }

    pthread_mutex_lock(&g_sched_mutex);
    g_sched_running = false;
    pthread_cond_signal(&g_sched_cond);
    pthread_mutex_unlock(&g_sched_mutex);

    pthread_join(g_sched_thread, NULL);
}
