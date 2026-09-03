#pragma once

#include <stdint.h>

typedef struct create_cashflow_cmd {
    int64_t     user_id;
    int64_t     source_asset_id;
    int64_t     target_asset_id;
    const char* name;
    const char* flow_type;
    const char* frequency;
    const char* start_date;
    const char* end_date;
    double      expected_amount;
    const char* note;
} create_cashflow_cmd_t;

typedef struct update_cashflow_cmd {
    int64_t     user_id;
    int64_t     id;
    int64_t     source_asset_id;
    int64_t     target_asset_id;
    const char* name;
    const char* flow_type;
    const char* frequency;
    const char* start_date;
    const char* end_date;
    double      expected_amount;
    const char* note;
} update_cashflow_cmd_t;

typedef struct confirm_cashflow_cmd {
    int64_t     user_id;
    int64_t     source_asset_id;
    int64_t     target_asset_id;
    double      amount;
    const char* date;
    const char* name;
    const char* note;
} confirm_cashflow_cmd_t;

typedef struct query_calendar_cmd {
    int64_t user_id;
    int     year;
    int     month;
} query_calendar_cmd_t;
