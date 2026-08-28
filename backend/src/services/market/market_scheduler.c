#include "services/market/market_scheduler.h"
#include "services/market_service.h"
#include "repositories/dca_repo.h"
#include "csilk/csilk.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static pthread_t        g_sched_thread;
static pthread_mutex_t  g_sched_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_sched_cond = PTHREAD_COND_INITIALIZER;
static bool             g_sched_running = false;
static csilk_db_pool_t* g_pool = NULL;

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
            /* Create pending execution (unique constraint on plan_id, period_date prevents duplicate) */
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
is_trading_hour(struct tm* tm_now)
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
is_nightly_settlement(struct tm* tm_now)
{
    /* Monday to Friday 21:30 - 21:35 */
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

        /* Check trading hours: sync every 15 minutes (900 seconds) */
        if (is_trading_hour(&tm_now) && (now - last_sync_time >= 900)) {
            should_sync = true;
            last_sync_time = now;
        }

        /* Check nightly settlement: sync once per evening */
        if (is_nightly_settlement(&tm_now) && (now - last_nightly_time >= 3600)) {
            should_sync = true;
            last_nightly_time = now;
            CSILK_LOG_I("Market scheduler triggering nightly fund settlement sync");
        }

        if (should_sync && g_pool) {
            pthread_mutex_unlock(&g_sched_mutex);
            int synced = 0, failed = 0;
            /* user_id = 0 syncs all users */
            market_service_do_sync_user(g_pool, 0, &synced, &failed);
            CSILK_LOG_I("Market scheduler completed sync: %d synced, %d failed", synced, failed);
            pthread_mutex_lock(&g_sched_mutex);
        }

        /* Wait 60 seconds or until stopped */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 60;
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
