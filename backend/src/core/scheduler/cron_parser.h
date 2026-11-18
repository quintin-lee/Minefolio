#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t minutes;       /* bits 0-59 */
    uint32_t hours;         /* bits 0-23 */
    uint32_t days_of_month; /* bits 1-31 */
    uint16_t months;        /* bits 1-12 */
    uint8_t  days_of_week;  /* bits 0-6 (0=Sun, 1=Mon, ..., 6=Sat) */
    bool     dom_star;      /* day of month was '*' */
    bool     dow_star;      /* day of week was '*' */
} cron_expr_t;

/**
 * Parse a standard 5-part cron expression:
 * "<minute> <hour> <day-of-month> <month> <day-of-week>"
 * Returns 0 on success, -1 on invalid expression.
 */
int cron_parse_expr(const char* expr, cron_expr_t* out_parsed);

/**
 * Calculate the next timestamp matching the cron expression strictly after `after_time`.
 * Returns (time_t)-1 if no match found within 5 years.
 */
time_t cron_next_after(const cron_expr_t* parsed, time_t after_time);

/**
 * Convenience helper: parses `expr`, calculates next time after `after_time`,
 * and formats it into `out_buf` as "YYYY-MM-DD HH:MM:SS" (in local time).
 * Returns 0 on success, -1 on error.
 */
int cron_format_next_str(const char* expr, time_t after_time, char* out_buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
