#include "core/scheduler/cron_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
parse_subtoken(const char* s, int min_val, int max_val, uint64_t* mask, bool is_dow, bool* is_star)
{
    if (!s || !*s) {
        return -1;
    }

    if (s[0] == '*') {
        if (is_star) {
            *is_star = true;
        }
        if (s[1] == '\0') {
            for (int i = min_val; i <= max_val; ++i) {
                int val = (is_dow && i == 7) ? 0 : i;
                *mask |= (1ULL << val);
            }
            return 0;
        } else if (s[1] == '/') {
            int step = atoi(s + 2);
            if (step <= 0) {
                return -1;
            }
            for (int i = min_val; i <= max_val; i += step) {
                int val = (is_dow && i == 7) ? 0 : i;
                *mask |= (1ULL << val);
            }
            return 0;
        }
        return -1;
    }

    /* Not starting with '*' */
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", s);

    int   step = 1;
    char* slash = strchr(buf, '/');
    if (slash) {
        *slash = '\0';
        step = atoi(slash + 1);
        if (step <= 0) {
            return -1;
        }
    }

    int   start_val = 0;
    int   end_val = 0;
    char* dash = strchr(buf, '-');
    if (dash) {
        *dash = '\0';
        if (!isdigit((unsigned char)buf[0]) || !isdigit((unsigned char)dash[1])) {
            return -1;
        }
        start_val = atoi(buf);
        end_val = atoi(dash + 1);
        if (start_val > end_val) {
            return -1;
        }
    } else {
        if (!isdigit((unsigned char)buf[0])) {
            return -1;
        }
        start_val = atoi(buf);
        end_val = slash ? max_val : start_val;
    }

    if (start_val < min_val || (start_val > max_val && !(is_dow && start_val == 7))) {
        return -1;
    }
    if (end_val < min_val || (end_val > max_val && !(is_dow && end_val == 7))) {
        return -1;
    }

    for (int i = start_val; i <= end_val; i += step) {
        int val = (is_dow && i == 7) ? 0 : i;
        if (val < min_val || val > max_val) {
            return -1;
        }
        *mask |= (1ULL << val);
    }

    return 0;
}

static int
parse_field(const char* field, int min_val, int max_val, uint64_t* mask, bool is_dow, bool* is_star)
{
    *mask = 0;
    if (is_star) {
        *is_star = false;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "%s", field);

    char* saveptr = NULL;
    char* token = strtok_r(buf, ",", &saveptr);
    int   tokens_count = 0;
    while (token) {
        bool tok_star = false;
        if (parse_subtoken(token, min_val, max_val, mask, is_dow, &tok_star) != 0) {
            return -1;
        }
        if (tok_star && tokens_count == 0 && !saveptr) {
            if (is_star) {
                *is_star = true;
            }
        }
        tokens_count++;
        token = strtok_r(NULL, ",", &saveptr);
    }

    if (tokens_count == 1 && strcmp(field, "*") == 0 && is_star) {
        *is_star = true;
    }

    return tokens_count > 0 ? 0 : -1;
}

int
cron_parse_expr(const char* expr, cron_expr_t* out_parsed)
{
    if (!expr || !out_parsed) {
        return -1;
    }
    memset(out_parsed, 0, sizeof(*out_parsed));

    /* Skip leading whitespace */
    while (*expr && isspace((unsigned char)*expr)) {
        expr++;
    }
    if (!*expr) {
        return -1;
    }

    char fields[5][64];
    int  fidx = 0;
    int  char_idx = 0;

    const char* p = expr;
    while (*p && fidx < 5) {
        if (isspace((unsigned char)*p)) {
            if (char_idx > 0) {
                fields[fidx][char_idx] = '\0';
                fidx++;
                char_idx = 0;
            }
        } else {
            if (char_idx >= (int)sizeof(fields[0]) - 1) {
                return -1;
            }
            fields[fidx][char_idx++] = *p;
        }
        p++;
    }
    if (char_idx > 0 && fidx < 5) {
        fields[fidx][char_idx] = '\0';
        fidx++;
    }

    if (fidx != 5) {
        return -1;
    }

    /* Check for trailing non-whitespace */
    while (*p) {
        if (!isspace((unsigned char)*p)) {
            return -1;
        }
        p++;
    }

    uint64_t mask = 0;

    /* 0: minute (0-59) */
    if (parse_field(fields[0], 0, 59, &mask, false, NULL) != 0) {
        return -1;
    }
    out_parsed->minutes = mask;

    /* 1: hour (0-23) */
    if (parse_field(fields[1], 0, 23, &mask, false, NULL) != 0) {
        return -1;
    }
    out_parsed->hours = (uint32_t)mask;

    /* 2: day of month (1-31) */
    bool dom_star = false;
    if (parse_field(fields[2], 1, 31, &mask, false, &dom_star) != 0) {
        return -1;
    }
    out_parsed->days_of_month = (uint32_t)mask;
    out_parsed->dom_star = dom_star;

    /* 3: month (1-12) */
    if (parse_field(fields[3], 1, 12, &mask, false, NULL) != 0) {
        return -1;
    }
    out_parsed->months = (uint16_t)mask;

    /* 4: day of week (0-6) */
    bool dow_star = false;
    if (parse_field(fields[4], 0, 7, &mask, true, &dow_star) != 0) {
        return -1;
    }
    out_parsed->days_of_week = (uint8_t)mask;
    out_parsed->dow_star = dow_star;

    return 0;
}

time_t
cron_next_after(const cron_expr_t* parsed, time_t after_time)
{
    if (!parsed) {
        return (time_t)-1;
    }

    /* Start from next minute strictly after after_time */
    time_t    cur = after_time;
    struct tm tm_buf;
    if (!localtime_r(&cur, &tm_buf)) {
        return (time_t)-1;
    }
    tm_buf.tm_sec = 0;
    tm_buf.tm_min += 1;
    tm_buf.tm_isdst = -1;
    cur = mktime(&tm_buf);
    if (cur == (time_t)-1) {
        return (time_t)-1;
    }

    int start_year = tm_buf.tm_year;
    int max_years = 5;

    while (1) {
        if (!localtime_r(&cur, &tm_buf)) {
            return (time_t)-1;
        }

        /* Prevent infinite loop */
        if (tm_buf.tm_year > start_year + max_years) {
            return (time_t)-1;
        }

        /* 1. Check Month */
        int mon = tm_buf.tm_mon + 1; /* 1-12 */
        if (!(parsed->months & (1U << mon))) {
            tm_buf.tm_mon += 1;
            tm_buf.tm_mday = 1;
            tm_buf.tm_hour = 0;
            tm_buf.tm_min = 0;
            tm_buf.tm_sec = 0;
            tm_buf.tm_isdst = -1;
            cur = mktime(&tm_buf);
            if (cur == (time_t)-1) {
                return (time_t)-1;
            }
            continue;
        }

        /* 2. Check Day */
        int  dom = tm_buf.tm_mday; /* 1-31 */
        int  dow = tm_buf.tm_wday; /* 0-6 */
        bool dom_match = (parsed->days_of_month & (1U << dom)) != 0;
        bool dow_match = (parsed->days_of_week & (1U << dow)) != 0;
        bool day_match = false;

        if (parsed->dom_star && parsed->dow_star) {
            day_match = true;
        } else if (!parsed->dom_star && parsed->dow_star) {
            day_match = dom_match;
        } else if (parsed->dom_star && !parsed->dow_star) {
            day_match = dow_match;
        } else {
            day_match = dom_match || dow_match;
        }

        if (!day_match) {
            tm_buf.tm_mday += 1;
            tm_buf.tm_hour = 0;
            tm_buf.tm_min = 0;
            tm_buf.tm_sec = 0;
            tm_buf.tm_isdst = -1;
            cur = mktime(&tm_buf);
            if (cur == (time_t)-1) {
                return (time_t)-1;
            }
            continue;
        }

        /* 3. Check Hour */
        int hour = tm_buf.tm_hour; /* 0-23 */
        if (!(parsed->hours & (1U << hour))) {
            tm_buf.tm_hour += 1;
            tm_buf.tm_min = 0;
            tm_buf.tm_sec = 0;
            tm_buf.tm_isdst = -1;
            cur = mktime(&tm_buf);
            if (cur == (time_t)-1) {
                return (time_t)-1;
            }
            continue;
        }

        /* 4. Check Minute */
        int min = tm_buf.tm_min; /* 0-59 */
        if (!(parsed->minutes & (1ULL << min))) {
            tm_buf.tm_min += 1;
            tm_buf.tm_sec = 0;
            tm_buf.tm_isdst = -1;
            cur = mktime(&tm_buf);
            if (cur == (time_t)-1) {
                return (time_t)-1;
            }
            continue;
        }

        /* All matched! */
        return cur;
    }
}

int
cron_format_next_str(const char* expr, time_t after_time, char* out_buf, size_t buf_len)
{
    if (!expr || !out_buf || buf_len < 20) {
        return -1;
    }

    cron_expr_t parsed;
    if (cron_parse_expr(expr, &parsed) != 0) {
        return -1;
    }

    time_t next_t = cron_next_after(&parsed, after_time);
    if (next_t == (time_t)-1) {
        return -1;
    }

    struct tm tm_buf;
    if (!localtime_r(&next_t, &tm_buf)) {
        return -1;
    }

    strftime(out_buf, buf_len, "%Y-%m-%d %H:%M:%S", &tm_buf);
    return 0;
}
