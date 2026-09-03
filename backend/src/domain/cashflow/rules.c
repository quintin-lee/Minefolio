#include "domain/cashflow/rules.h"
#include <stdio.h>
#include <string.h>

int mf_cashflow_rule_validate(const mf_cashflow_schedule_t* s, char* err_buf, size_t err_len) {
    if (!s) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Cashflow schedule is NULL");
        return -1;
    }
    if (s->user_id <= 0) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Invalid user_id");
        return -1;
    }
    if (s->source_asset_id <= 0 || s->target_asset_id <= 0) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "source_asset_id and target_asset_id must be valid");
        return -1;
    }
    if (!s->name[0]) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Schedule name cannot be empty");
        return -1;
    }
    if (!s->start_date[0]) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "start_date cannot be empty");
        return -1;
    }
    if (!money_is_positive(s->expected_amount)) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "expected_amount must be positive");
        return -1;
    }
    return 0;
}

int mf_cashflow_rule_annual_factor(const char* freq, double* out_factor) {
    if (!out_factor) return -1;
    if (!freq || !freq[0]) {
        *out_factor = 12.0;
        return 0;
    }

    if (strcmp(freq, "monthly") == 0) {
        *out_factor = 12.0;
    } else if (strcmp(freq, "quarterly") == 0) {
        *out_factor = 4.0;
    } else if (strcmp(freq, "semi_annual") == 0) {
        *out_factor = 2.0;
    } else if (strcmp(freq, "annual") == 0 || strcmp(freq, "once") == 0) {
        *out_factor = 1.0;
    } else {
        *out_factor = 12.0;
    }
    return 0;
}

bool mf_cashflow_rule_matches_month(const char* freq, const char* start_date, const char* end_date,
                                   int year, int month, int* out_day) {
    if (!start_date || !start_date[0]) return false;
    if (!freq) freq = "monthly";

    int s_year = 2026, s_month = 1, s_day = 1;
    if (sscanf(start_date, "%d-%d-%d", &s_year, &s_month, &s_day) < 3) {
        return false;
    }

    bool matches = false;
    int target_day = s_day > 28 ? 28 : (s_day < 1 ? 1 : s_day);

    if (strcmp(freq, "once") == 0) {
        if (s_year == year && s_month == month) {
            matches = true;
        }
    } else if (strcmp(freq, "monthly") == 0) {
        if (year > s_year || (year == s_year && month >= s_month)) {
            matches = true;
        }
    } else if (strcmp(freq, "quarterly") == 0) {
        int total_months = (year - s_year) * 12 + (month - s_month);
        if (total_months >= 0 && (total_months % 3) == 0) {
            matches = true;
        }
    } else if (strcmp(freq, "semi_annual") == 0) {
        int total_months = (year - s_year) * 12 + (month - s_month);
        if (total_months >= 0 && (total_months % 6) == 0) {
            matches = true;
        }
    } else if (strcmp(freq, "annual") == 0) {
        if (year >= s_year && month == s_month) {
            matches = true;
        }
    }

    if (!matches) return false;

    char pdate[32];
    snprintf(pdate, sizeof(pdate), "%04d-%02d-%02d", year, month, target_day);
    if (end_date && end_date[0] && strcmp(pdate, end_date) > 0) {
        return false;
    }

    if (out_day) *out_day = target_day;
    return true;
}
