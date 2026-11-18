#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/scheduler/cron_parser.h"

static time_t
make_time(int year, int mon, int mday, int hour, int min, int sec)
{
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year = year - 1900;
    tm_val.tm_mon = mon - 1;
    tm_val.tm_mday = mday;
    tm_val.tm_hour = hour;
    tm_val.tm_min = min;
    tm_val.tm_sec = sec;
    tm_val.tm_isdst = -1;
    return mktime(&tm_val);
}

int
main(void)
{
    printf("=== Starting Cron Parser Unit Tests ===\n");

    /* 1. Test parsing valid expressions */
    cron_expr_t expr;
    assert(cron_parse_expr("* * * * *", &expr) == 0);
    assert(expr.minutes == 0xFFFFFFFFFFFFFFFULL); /* 60 bits */
    assert(expr.hours == 0xFFFFFFU);              /* 24 bits */
    assert(expr.dom_star == true);
    assert(expr.dow_star == true);

    /* 2. Test invalid expressions */
    assert(cron_parse_expr(NULL, &expr) == -1);
    assert(cron_parse_expr("", &expr) == -1);
    assert(cron_parse_expr("* * * *", &expr) == -1);       /* 4 fields */
    assert(cron_parse_expr("* * * * * *", &expr) == -1);   /* 6 fields */
    assert(cron_parse_expr("60 * * * *", &expr) == -1);    /* minute 60 > 59 */
    assert(cron_parse_expr("* 24 * * *", &expr) == -1);    /* hour 24 > 23 */
    assert(cron_parse_expr("* * 32 * *", &expr) == -1);    /* dom 32 > 31 */
    assert(cron_parse_expr("* * * 13 *", &expr) == -1);    /* month 13 > 12 */
    assert(cron_parse_expr("* * * * 8", &expr) == -1);     /* dow 8 > 7 */
    assert(cron_parse_expr("*/0 * * * *", &expr) == -1);   /* step 0 */
    assert(cron_parse_expr("5-2 * * * *", &expr) == -1);   /* inverted range */

    /* 3. Test next minute for * * * * * */
    assert(cron_parse_expr("* * * * *", &expr) == 0);
    time_t t0 = make_time(2026, 11, 18, 14, 30, 25);
    time_t next_t = cron_next_after(&expr, t0);
    assert(next_t == make_time(2026, 11, 18, 14, 31, 0));

    /* 4. Test step: */
    assert(cron_parse_expr("*/15 * * * *", &expr) == 0);
    t0 = make_time(2026, 11, 18, 14, 16, 0);
    next_t = cron_next_after(&expr, t0);
    assert(next_t == make_time(2026, 11, 18, 14, 30, 0));

    t0 = make_time(2026, 11, 18, 14, 45, 0);
    next_t = cron_next_after(&expr, t0);
    assert(next_t == make_time(2026, 11, 18, 15, 0, 0));

    /* 5. Test specific hour/min and comma list: 0 8,12,18 * * * */
    assert(cron_parse_expr("0 8,12,18 * * *", &expr) == 0);
    t0 = make_time(2026, 11, 18, 7, 59, 59);
    next_t = cron_next_after(&expr, t0);
    assert(next_t == make_time(2026, 11, 18, 8, 0, 0));

    t0 = make_time(2026, 11, 18, 8, 0, 0);
    next_t = cron_next_after(&expr, t0);
    assert(next_t == make_time(2026, 11, 18, 12, 0, 0));

    t0 = make_time(2026, 11, 18, 18, 0, 0);
    next_t = cron_next_after(&expr, t0);
    assert(next_t == make_time(2026, 11, 19, 8, 0, 0));

    /* 6. Test weekday restriction: 0 5 * * 1-5 (Mon-Fri) */
    assert(cron_parse_expr("0 5 * * 1-5", &expr) == 0);
    /* 2026-11-20 is Friday */
    t0 = make_time(2026, 11, 20, 5, 0, 0);
    next_t = cron_next_after(&expr, t0);
    /* Next run should skip Saturday (21) and Sunday (22), and run on Monday (23) */
    assert(next_t == make_time(2026, 11, 23, 5, 0, 0));

    /* 7. Test Sunday equivalence: 7 == 0 */
    cron_expr_t expr_sun0, expr_sun7;
    assert(cron_parse_expr("0 0 * * 0", &expr_sun0) == 0);
    assert(cron_parse_expr("0 0 * * 7", &expr_sun7) == 0);
    assert(expr_sun0.days_of_week == expr_sun7.days_of_week);
    assert((expr_sun7.days_of_week & 1) != 0); /* bit 0 set */

    /* 8. Test month / year rollover */
    assert(cron_parse_expr("0 0 1 1 *", &expr) == 0); /* Jan 1st 00:00 */
    t0 = make_time(2026, 11, 18, 10, 0, 0);
    next_t = cron_next_after(&expr, t0);
    assert(next_t == make_time(2027, 1, 1, 0, 0, 0));

    /* 9. Test cron_format_next_str */
    char buf[64];
    assert(cron_format_next_str("0 9 * * 1-5", make_time(2026, 11, 18, 8, 0, 0), buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "2026-11-18 09:00:00") == 0);

    printf("=== Cron Parser Unit Tests Passed! ===\n");
    return 0;
}
