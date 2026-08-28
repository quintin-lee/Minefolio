#pragma once
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 定投计划模型
 */
typedef struct {
    int64_t id;
    int64_t user_id;
    int64_t target_asset_id;
    int64_t funding_asset_id;
    char    name[128];
    char    frequency[32];      /* "weekly", "biweekly", "monthly" */
    int     day_of_period;      /* 1-7 for weekly, 1-31 for monthly */
    double  amount;
    double  target_profit_rate; /* e.g. 0.15 for 15% */
    double  target_total_amount;
    int     target_total_periods;
    char    status[32];         /* "active", "paused", "completed" */
    char    note[256];
    char    created_at[32];
    char    updated_at[32];
} dca_plan_t;

/**
 * @brief 定投执行记录模型
 */
typedef struct {
    int64_t id;
    int64_t plan_id;
    int64_t user_id;
    char    period_date[16]; /* "YYYY-MM-DD" */
    double  planned_amount;
    double  actual_amount;
    double  executed_price;
    double  executed_quantity;
    int64_t transaction_id;
    char    status[32]; /* "pending", "confirmed", "skipped" */
    char    created_at[32];
    char    updated_at[32];
} dca_execution_t;

/**
 * @brief 周期性被动现金流计划模型
 */
typedef struct {
    int64_t id;
    int64_t user_id;
    int64_t source_asset_id;
    int64_t target_asset_id;
    char    name[128];
    char    flow_type[32];  /* "dividend", "interest", "rent", "maturity" */
    char    frequency[32];  /* "once", "monthly", "quarterly", "semi_annual", "annual" */
    char    start_date[16]; /* "YYYY-MM-DD" */
    char    end_date[16];   /* "YYYY-MM-DD" or "" */
    double  expected_amount;
    char    status[32];     /* "active", "completed", "cancelled" */
    char    note[256];
    char    created_at[32];
    char    updated_at[32];
} cashflow_schedule_t;
