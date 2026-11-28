#include "services/ai/tools/budget_tool.h"
#include "services/ai/tools/tool_spec.h"
#include "services/ai/tools/tool_helpers.h"
#include "application/cashflow/usecases.h"
#include "application/dca/usecases.h"
#include "infrastructure/repositories/ai_repo_impl.h"
#include "common/db.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int
days_in_month(int y, int m)
{
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) {
        return 30;
    }
    int d = mdays[m - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) {
        d = 29;
    }
    return d;
}

static char*
exec_get_budget_status(const ai_tool_t*         tool,
                       const ai_tool_context_t* ctx,
                       const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int now_y = tm_now.tm_year + 1900;
    int now_m = tm_now.tm_mon + 1;
    int now_d = tm_now.tm_mday;

    int         yr = now_y, mo = now_m;
    const char* m_in = args ? csilk_json_get_string(args, "month") : NULL;
    if (m_in && m_in[0]) {
        if (sscanf(m_in, "%d-%d", &yr, &mo) != 2 || yr < 1900 || mo < 1 || mo > 12) {
            return ai_tool_json_error("invalid month format, expected YYYY-MM", 1002);
        }
    }

    double multiplier = 1.2;
    if (args && csilk_json_get(args, "multiplier")) {
        double mult_in = db_get_num(args, "multiplier");
        if (mult_in > 0.0) {
            multiplier = mult_in;
            if (multiplier < 0.5) {
                multiplier = 0.5;
            }
            if (multiplier > 3.0) {
                multiplier = 3.0;
            }
        }
    }

    double monthly_limit = args ? db_get_num(args, "monthly_budget_limit") : 0.0;

    char month_str[16], pat[32];
    snprintf(month_str, sizeof(month_str), "%04d-%02d", yr, mo);
    snprintf(pat, sizeof(pat), "%s%%", month_str);

    int    dim = days_in_month(yr, mo);
    int    elapsed_days = now_d;
    double progress = 1.0;
    if (yr < now_y || (yr == now_y && mo < now_m)) {
        elapsed_days = dim;
        progress = 1.0;
    } else if (yr > now_y || (yr == now_y && mo > now_m)) {
        elapsed_days = 0;
        progress = 0.05;
    } else {
        progress = dim > 0 ? (double)now_d / (double)dim : 1.0;
        if (progress < 0.05) {
            progress = 0.05;
        }
        if (progress > 1.0) {
            progress = 1.0;
        }
    }

    csilk_json_t* cur_cats = mf_ai_repo_daily_expense_monthly_by_category(ctx->pool, ctx->user_id, pat);
    csilk_json_t* hist_avg = mf_ai_repo_daily_expense_hist_avg_by_category(ctx->pool, ctx->user_id, pat);

    csilk_json_t* cat_arr = csilk_json_array();
    double        spent_amount = 0.0;
    double        total_budget_amount = 0.0;
    double        total_projected_amount = 0.0;
    int           danger_count = 0;
    int           warning_count = 0;

    if (cur_cats && csilk_json_is_array(cur_cats)) {
        size_t n = csilk_json_array_size(cur_cats);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* row = csilk_json_array_get(cur_cats, i);
            const char*         et = csilk_json_get_string(row, "expense_type");
            if (et && strcmp(et, "expense") != 0) {
                continue;
            }
            const char* cname = csilk_json_get_string(row, "category_name");
            if (!cname || !cname[0]) {
                cname = "未分类";
            }
            double cur_amt = db_get_num(row, "amount");
            double avg_amt = 0.0;
            if (hist_avg && csilk_json_is_array(hist_avg)) {
                size_t hn = csilk_json_array_size(hist_avg);
                for (size_t j = 0; j < hn; j++) {
                    const csilk_json_t* h = csilk_json_array_get(hist_avg, j);
                    const char*         hname = csilk_json_get_string(h, "category_name");
                    if (hname && strcmp(hname, cname) == 0) {
                        avg_amt = db_get_num(h, "avg_amount");
                        break;
                    }
                }
            }

            double budget_amt = 0.0;
            if (avg_amt > 0.0) {
                budget_amt = avg_amt * multiplier;
            } else {
                budget_amt = cur_amt > 0.0 ? (cur_amt / progress) : 0.0;
                if (budget_amt < cur_amt) {
                    budget_amt = cur_amt * 1.5;
                }
            }
            double      projected_amt = (progress > 0.0) ? (cur_amt / progress) : cur_amt;
            double      usage_pct = (budget_amt > 0.0) ? (cur_amt / budget_amt) * 100.0 : 0.0;
            double      proj_pct = (budget_amt > 0.0) ? (projected_amt / budget_amt) * 100.0 : 0.0;
            const char* risk = "safe";
            if (proj_pct >= 100.0) {
                risk = "danger";
                danger_count++;
            } else if (proj_pct >= 80.0) {
                risk = "warning";
                warning_count++;
            }

            csilk_json_t* item = csilk_json_object();
            csilk_json_add_string(item, "category_name", cname);
            csilk_json_add_number(item, "spent_amount", cur_amt);
            csilk_json_add_number(item, "hist_avg_amount", avg_amt);
            csilk_json_add_number(item, "budget_amount", budget_amt);
            csilk_json_add_number(item, "projected_amount", projected_amt);
            csilk_json_add_number(item, "usage_pct", usage_pct);
            csilk_json_add_number(item, "projected_pct", proj_pct);
            csilk_json_add_string(item, "risk", risk);
            csilk_json_array_append(cat_arr, item);

            spent_amount += cur_amt;
            total_budget_amount += budget_amt;
            total_projected_amount += projected_amt;
        }
    }
    if (cur_cats) {
        csilk_json_free(cur_cats);
    }
    if (hist_avg) {
        csilk_json_free(hist_avg);
    }

    if (monthly_limit > 0.0) {
        total_budget_amount = monthly_limit;
    }

    double remaining_amount = total_budget_amount - spent_amount;
    double total_usage_pct =
        (total_budget_amount > 0.0) ? (spent_amount / total_budget_amount) * 100.0 : 0.0;
    double total_proj_pct =
        (total_budget_amount > 0.0) ? (total_projected_amount / total_budget_amount) * 100.0 : 0.0;

    int    days_left = dim - elapsed_days;
    double suggested_daily_limit =
        (days_left > 0 && remaining_amount > 0.0) ? (remaining_amount / (double)days_left) : 0.0;

    const char* overall_risk = "safe";
    if (total_proj_pct >= 100.0 || danger_count > 0 || remaining_amount < 0.0) {
        overall_risk = "danger";
    } else if (total_proj_pct >= 80.0 || warning_count > 0) {
        overall_risk = "warning";
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_string(res, "month", month_str);
    csilk_json_add_number(res, "elapsed_days", (double)elapsed_days);
    csilk_json_add_number(res, "days_in_month", (double)dim);
    csilk_json_add_number(res, "month_progress_pct", progress * 100.0);
    csilk_json_add_number(res, "multiplier", multiplier);
    csilk_json_add_number(res, "spent_amount", spent_amount);
    csilk_json_add_number(res, "total_budget_amount", total_budget_amount);
    csilk_json_add_number(res, "remaining_amount", remaining_amount);
    csilk_json_add_number(res, "projected_amount", total_projected_amount);
    csilk_json_add_number(res, "total_usage_pct", total_usage_pct);
    csilk_json_add_number(res, "total_projected_pct", total_proj_pct);
    csilk_json_add_number(res, "suggested_daily_limit_amount", suggested_daily_limit);
    csilk_json_add_string(res, "overall_risk", overall_risk);
    csilk_json_add_number(res, "danger_count", (double)danger_count);
    csilk_json_add_number(res, "warning_count", (double)warning_count);
    csilk_json_add_array(res, "categories", cat_arr);
    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_savings_goals(const ai_tool_t*         tool,
                       const ai_tool_context_t* ctx,
                       const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    bool include_completed = true;
    if (args && csilk_json_get(args, "include_completed")) {
        include_completed = csilk_json_get_bool(args, "include_completed");
    }

    csilk_json_t* rows = mf_ai_repo_savings_goals_list(ctx->pool, ctx->user_id);
    csilk_json_t* goals = csilk_json_array();

    time_t    now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    tm_now.tm_hour = 0;
    tm_now.tm_min = 0;
    tm_now.tm_sec = 0;
    time_t tnow = mktime(&tm_now);

    double total_target = 0.0, total_current = 0.0;
    int    overdue_count = 0, completed_count = 0;

    if (rows && csilk_json_is_array(rows)) {
        size_t n = csilk_json_array_size(rows);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* r = csilk_json_array_get(rows, i);
            double              tgt = db_get_num(r, "target_amount");
            double              cur = db_get_num(r, "current_amount");
            if (tgt < 0.0) {
                tgt = 0.0;
            }
            if (cur < 0.0) {
                cur = 0.0;
            }
            double prog = (tgt > 0.0) ? (cur / tgt) * 100.0 : 0.0;
            if (prog > 100.0) {
                prog = 100.0;
            }
            bool is_completed = (tgt > 0.0 && cur >= tgt);
            if (is_completed) {
                completed_count++;
            }
            if (!include_completed && is_completed) {
                continue;
            }

            const char* deadline = csilk_json_get_string(r, "deadline");
            if (!deadline) {
                deadline = "";
            }
            bool is_overdue = false;
            int  days_left = -1;
            if (deadline[0] && !is_completed) {
                int y = 0, m = 0, d = 0;
                if (sscanf(deadline, "%d-%d-%d", &y, &m, &d) == 3) {
                    struct tm dl = {0};
                    dl.tm_year = y - 1900;
                    dl.tm_mon = m - 1;
                    dl.tm_mday = d;
                    time_t tdl = mktime(&dl);
                    double diff = difftime(tdl, tnow) / 86400.0;
                    days_left = (int)diff;
                    if (diff < 0.0) {
                        is_overdue = true;
                        overdue_count++;
                    }
                }
            }

            total_target += tgt;
            total_current += cur;

            csilk_json_t* o = csilk_json_object();
            csilk_json_add_number(o, "id", (double)db_get_int(r, "id"));
            csilk_json_add_string(
                o,
                "name",
                csilk_json_get_string(r, "name") ? csilk_json_get_string(r, "name") : "");
            csilk_json_add_number(o, "target_amount", tgt);
            csilk_json_add_number(o, "current_amount", cur);
            csilk_json_add_number(o, "remaining_amount", tgt > cur ? tgt - cur : 0.0);
            csilk_json_add_number(o, "progress_pct", prog);
            csilk_json_add_string(o, "deadline", deadline);
            csilk_json_add_number(o, "days_left", (double)days_left);
            csilk_json_add_bool(o, "is_overdue", is_overdue);
            csilk_json_add_bool(o, "is_completed", is_completed);
            csilk_json_add_string(
                o,
                "note",
                csilk_json_get_string(r, "note") ? csilk_json_get_string(r, "note") : "");
            csilk_json_array_append(goals, o);
        }
        csilk_json_free(rows);
    }

    double overall_pct = (total_target > 0.0) ? (total_current / total_target) * 100.0 : 0.0;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total", (double)csilk_json_array_size(goals));
    csilk_json_add_number(res, "total_target_amount", total_target);
    csilk_json_add_number(res, "total_current_amount", total_current);
    csilk_json_add_number(res, "overall_progress_pct", overall_pct);
    csilk_json_add_number(res, "overdue_count", (double)overdue_count);
    csilk_json_add_number(res, "completed_count", (double)completed_count);
    csilk_json_add_array(res, "goals", goals);
    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_dca_plans(const ai_tool_t* tool, const ai_tool_context_t* ctx, const csilk_json_t* args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    const char* status = args ? csilk_json_get_string(args, "status") : NULL;
    if (status && !status[0]) {
        status = NULL;
    }
    int64_t ledger_id = ai_tool_resolve_ledger_id(ctx, args, false);
    int64_t page = 1, page_size = 20;
    ai_tool_parse_pagination(args, 20, 100, &page, &page_size);

    int64_t       total = 0;
    csilk_json_t* plans =
        dca_usecase_list_plans(ctx->pool, ctx->user_id, ledger_id, page, page_size, status, &total);
    csilk_json_t* summary = dca_usecase_plan_summary(ctx->pool, ctx->user_id, ledger_id);

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "ledger_id", (double)ledger_id);
    csilk_json_add_number(res, "total", (double)total);
    csilk_json_add_number(res, "page", (double)page);
    csilk_json_add_number(res, "page_size", (double)page_size);
    if (summary) {
        csilk_json_add_object(res, "summary", summary);
    }
    csilk_json_add_array(res, "plans", plans ? plans : csilk_json_array());
    return ai_tool_json_serialize_free(res);
}

static char*
exec_get_cashflow_schedules(const ai_tool_t*         tool,
                            const ai_tool_context_t* ctx,
                            const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    int64_t ledger_id = ai_tool_resolve_ledger_id(ctx, args, false);
    int64_t page = 1, page_size = 20;
    ai_tool_parse_pagination(args, 20, 100, &page, &page_size);

    csilk_json_t*             list = NULL;
    int64_t                   total = 0;
    cashflow_usecase_result_t cres = {0};
    if (cashflow_usecase_list_schedules_paged(
            ctx->pool, ctx->user_id, ledger_id, page, page_size, &list, &total, &cres) != 0) {
        if (list) {
            csilk_json_free(list);
        }
        return ai_tool_json_error(cres.message[0] ? cres.message : "failed to query schedules",
                                  cres.code);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "ledger_id", (double)ledger_id);
    csilk_json_add_number(res, "total", (double)total);
    csilk_json_add_number(res, "page", (double)page);
    csilk_json_add_number(res, "page_size", (double)page_size);
    csilk_json_add_array(res, "schedules", list ? list : csilk_json_array());
    return ai_tool_json_serialize_free(res);
}

static char*
exec_propose_savings_goal(const ai_tool_t*         tool,
                          const ai_tool_context_t* ctx,
                          const csilk_json_t*      args)
{
    (void)tool;
    if (!ai_tool_ctx_valid(ctx)) {
        return ai_tool_json_error("invalid context", 0);
    }

    const char* name = args ? csilk_json_get_string(args, "name") : NULL;
    double      target_amount = args ? db_get_num(args, "target_amount") : 0.0;
    double      current_amount = args ? db_get_num(args, "current_amount") : 0.0;
    const char* deadline = args ? csilk_json_get_string(args, "deadline") : NULL;
    const char* note = args ? csilk_json_get_string(args, "note") : NULL;
    int64_t     goal_id = args ? db_get_int(args, "goal_id") : 0;

    if (!name || !name[0]) {
        return ai_tool_json_error("目标名称不能为空", 1002);
    }
    if (target_amount <= 0.0) {
        return ai_tool_json_error("target_amount must be positive", 1002);
    }
    if (current_amount < 0.0) {
        return ai_tool_json_error("current_amount cannot be negative", 1002);
    }

    csilk_json_t* proposed_args = csilk_json_object();
    csilk_json_add_number(proposed_args, "goal_id", (double)(goal_id > 0 ? goal_id : 0));
    csilk_json_add_string(proposed_args, "name", name);
    csilk_json_add_number(proposed_args, "target_amount", target_amount);
    csilk_json_add_number(proposed_args, "current_amount", current_amount);
    csilk_json_add_string(proposed_args, "deadline", deadline ? deadline : "");
    csilk_json_add_string(proposed_args, "note", note ? note : "");

    char* out = ai_tool_propose_finalize(ctx,
                                         "savings_goal",
                                         "confirm_proposed_savings_goal",
                                         AI_RISK_MEDIUM,
                                         proposed_args,
                                         NULL);
    csilk_json_free(proposed_args);
    return out;
}

static char*
exec_confirm_proposed_savings_goal(const ai_tool_t*         tool,
                                   const ai_tool_context_t* ctx,
                                   const csilk_json_t*      args)
{
    (void)tool;
    char* verr = ai_tool_confirm_verify(ctx, "confirm_proposed_savings_goal", args);
    if (verr) {
        return verr;
    }

    int64_t     goal_id = args ? db_get_int(args, "goal_id") : 0;
    const char* name = args ? csilk_json_get_string(args, "name") : NULL;
    double      target_amount = args ? db_get_num(args, "target_amount") : 0.0;
    double      current_amount = args ? db_get_num(args, "current_amount") : 0.0;
    const char* deadline = args ? csilk_json_get_string(args, "deadline") : "";
    const char* note = args ? csilk_json_get_string(args, "note") : "";

    int64_t saved_id = 0;
    if (mf_ai_repo_savings_goal_upsert(
            ctx->pool, ctx->user_id, goal_id, name, target_amount, current_amount, deadline, note,
            &saved_id) != 0 ||
        saved_id <= 0) {
        return ai_tool_json_error(goal_id > 0 ? "目标不存在或更新失败" : "创建储蓄目标失败", 1003);
    }

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_bool(res, "success", true);
    csilk_json_add_number(res, "id", (double)saved_id);
    csilk_json_add_string(res, "name", name ? name : "");
    csilk_json_add_string(res, "message", goal_id > 0 ? "储蓄目标已更新" : "储蓄目标创建成功");
    return ai_tool_json_serialize_free(res);
}

static const ai_tool_spec_t s_budget_tool_specs[] = {
    {
        .name = "get_budget_status",
        .description = "读取指定月份的分类预算执行进度、历史消费均值基准、月末外推与超支风险预警",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_budget_status,
        .param_count = 3,
        .params = {
            AI_TOOL_PARAM("month", "string", "月份 YYYY-MM，默认当月"),
            AI_TOOL_PARAM("multiplier", "number", "基于历史月均消费的预算系数，默认 1.2（范围 0.5-3.0）"),
            AI_TOOL_PARAM("monthly_budget_limit", "number", "可选月度总预算上限金额（覆盖自动基准）"),
        },
    },
    {
        .name = "get_savings_goals",
        .description = "查询当前用户的储蓄与理财目标列表、完成进度、剩余金额与到期状态",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_savings_goals,
        .param_count = 1,
        .params = {
            AI_TOOL_PARAM("include_completed", "boolean", "是否包含已完成目标，默认 true"),
        },
    },
    {
        .name = "get_dca_plans",
        .description = "查询用户的基金/股票定投计划列表、投入进度、收益率与止盈达标情况",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_dca_plans,
        .param_count = 4,
        .params = {
            AI_TOOL_PARAM_ENUM("status", "计划状态筛选：active, paused, completed", false,
                               "active", "paused", "completed"),
            AI_TOOL_PARAM("ledger_id", "integer", "可选账本 ID"),
            AI_TOOL_PARAM("page", "integer", "当前页码，默认 1"),
            AI_TOOL_PARAM("page_size", "integer", "每页条数，默认 20"),
        },
    },
    {
        .name = "get_cashflow_schedules",
        .description = "查询用户的周期性被动现金流计划（股息、利息、房租、到期兑付等）",
        .permission = AI_PERM_READ,
        .risk = AI_RISK_LOW,
        .execute = exec_get_cashflow_schedules,
        .param_count = 3,
        .params = {
            AI_TOOL_PARAM("ledger_id", "integer", "可选账本 ID"),
            AI_TOOL_PARAM("page", "integer", "当前页码，默认 1"),
            AI_TOOL_PARAM("page_size", "integer", "每页条数，默认 20"),
        },
    },
    {
        .name = "propose_savings_goal",
        .description = "拟创建或更新储蓄/预算目标，生成防篡改确认草案；成功后须输出 ```action 卡片"
                       "（action_type: savings_goal）由用户确认后入库",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_MEDIUM,
        .is_mutation = true,
        .requires_id = true,
        .execute = exec_propose_savings_goal,
        .param_count = 6,
        .params = {
            AI_TOOL_PARAM_REQ("name", "string", "目标名称（如 应急备用金、年度旅行基金）"),
            AI_TOOL_PARAM_REQ("target_amount", "number", "目标金额（必须大于 0）"),
            AI_TOOL_PARAM("current_amount", "number", "当前已攒金额，默认 0"),
            AI_TOOL_PARAM("deadline", "string", "可选截止日期 YYYY-MM-DD"),
            AI_TOOL_PARAM("note", "string", "可选备注说明"),
            AI_TOOL_PARAM("goal_id", "integer", "可选目标 ID（>0 为更新已有目标，不填或 0 为新建）"),
        },
    },
    {
        .name = "confirm_proposed_savings_goal",
        .description = "用户确认保存储蓄/预算目标，校验确认令牌后入库",
        .action_type = "savings_goal",
        .permission = AI_PERM_WRITE,
        .risk = AI_RISK_HIGH,
        .is_mutation = true,
        .requires_id = true,
        .internal_only = true,
        .execute = exec_confirm_proposed_savings_goal,
        .param_count = 7,
        .params = {
            AI_TOOL_PARAM("goal_id", "integer", "目标 ID"),
            AI_TOOL_PARAM_REQ("name", "string", "目标名称"),
            AI_TOOL_PARAM_REQ("target_amount", "number", "目标金额"),
            AI_TOOL_PARAM("current_amount", "number", "当前已攒金额"),
            AI_TOOL_PARAM("deadline", "string", "截止日期 YYYY-MM-DD"),
            AI_TOOL_PARAM("note", "string", "备注"),
            AI_TOOL_PARAM_REQ("draft_token", "string", "防伪确认令牌"),
        },
    },
};

void
ai_tool_budget_register_all(void)
{
    ai_tool_register_specs(s_budget_tool_specs,
                           sizeof(s_budget_tool_specs) / sizeof(s_budget_tool_specs[0]));
}
