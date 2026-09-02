#include "services/ai/workflows/financial_health.h"
#include "repositories/asset_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double
fh_get_user_avg_monthly_burn(csilk_db_pool_t* pool, int64_t user_id)
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

static double
fh_get_asset_val(const csilk_json_t* a)
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

/* ========================================================================= */
/*  Workflow: Health Score (wf_health_score)                                 */
/* ========================================================================= */

static char*
step_hs_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)params;
    (void)ctx_json;
    if (!pool) {
        return strdup("{\"error\":\"db not ready\"}");
    }

    double liquid = 0.0;
    double total_assets = 0.0, total_debt = 0.0;

    int64_t       tot = 0;
    csilk_json_t* arr = asset_list(pool, user_id, 1, 200, NULL, &tot);
    if (arr && csilk_json_is_array(arr)) {
        size_t n = csilk_json_array_size(arr);
        for (size_t i = 0; i < n; i++) {
            const csilk_json_t* a = csilk_json_array_get(arr, (int)i);
            if (!a) {
                continue;
            }
            const char* typ = csilk_json_get_string(a, "asset_type") ?: "";
            int64_t     pid = (int64_t)db_get_num(a, "parent_id");
            if (pid != 0) {
                continue;
            }
            double v = fh_get_asset_val(a);
            if (v < 0) {
                v = -v;
            }

            int is_liab = (strcmp(typ, "loan") == 0 || strcmp(typ, "credit_card") == 0 ||
                           strcmp(typ, "other_liability") == 0);
            if (is_liab) {
                total_debt += v;
            } else {
                total_assets += v;
                if (strcmp(typ, "cash") == 0 || strcmp(typ, "bank") == 0 ||
                    strcmp(typ, "other_asset") == 0) {
                    liquid += v;
                }
            }
        }
    }
    if (arr) {
        csilk_json_free(arr);
    }

    double avg_burn = fh_get_user_avg_monthly_burn(pool, user_id);
    double avg_income = 0.0;
    {
        char uid2[32];
        snprintf(uid2, sizeof(uid2), "%lld", (long long)user_id);
        const char*   params2[] = {uid2, NULL};
        csilk_json_t* rows = csilk_db_query_param_json(
            pool,
            "SELECT AVG(mv) as avg_income FROM ("
            "  SELECT substr(expense_date,1,7) as ym, SUM(amount) as mv "
            "  FROM daily_expenses WHERE user_id=? AND expense_type='income' AND amount>0 "
            "  GROUP BY ym"
            ") t",
            params2);
        if (rows && csilk_json_array_size(rows) > 0) {
            avg_income = db_get_num(csilk_json_array_get(rows, 0), "avg_income");
        }
        if (rows) {
            csilk_json_free(rows);
        }
    }
    double savings_rate = 0.0;
    if (avg_income > 0.0) {
        savings_rate = (avg_income - avg_burn) / avg_income * 100.0;
        if (savings_rate < 0.0) {
            savings_rate = 0.0;
        }
    }
    double emergency_months = (avg_burn > 0.0) ? (liquid / avg_burn) : (liquid > 0.0 ? 12.0 : 0.0);
    double debt_ratio = (total_assets + total_debt) > 0.0
                            ? (total_debt / (total_assets + total_debt) * 100.0)
                            : 0.0;

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "liquid_cash", liquid);
    csilk_json_add_number(out, "total_assets", total_assets);
    csilk_json_add_number(out, "total_debt", total_debt);
    csilk_json_add_number(out, "avg_burn", avg_burn);
    csilk_json_add_number(out, "avg_income", avg_income);
    csilk_json_add_number(out, "savings_rate", savings_rate);
    csilk_json_add_number(out, "emergency_months", emergency_months);
    csilk_json_add_number(out, "debt_ratio", debt_ratio);
    size_t sl = 0;
    char*  s = csilk_json_serialize(out, &sl);
    csilk_json_free(out);
    return s ? s : strdup("{}");
}

static char*
step_hs_score(csilk_db_pool_t*    pool,
              int64_t             user_id,
              const csilk_json_t* params,
              const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* ctx = NULL;
    if (ctx_json && ctx_json[0]) {
        ctx = csilk_json_parse(ctx_json);
    }
    csilk_json_t* col = ctx ? csilk_json_get(ctx, "hs_collect") : NULL;
    double        emergency = col ? db_get_num(col, "emergency_months") : 0;
    double        debt_ratio = col ? db_get_num(col, "debt_ratio") : 0;
    double        savings_rate = col ? db_get_num(col, "savings_rate") : 0;
    double        avg_income = col ? db_get_num(col, "avg_income") : 0;
    double        avg_burn = col ? db_get_num(col, "avg_burn") : 0;

    int s_liquidity = 0;
    if (emergency >= 6) {
        s_liquidity = 25;
    } else if (emergency >= 3) {
        s_liquidity = 18;
    } else if (emergency >= 1) {
        s_liquidity = 10;
    } else if (emergency >= 0.5) {
        s_liquidity = 5;
    }

    int s_debt = 0;
    if (debt_ratio < 15) {
        s_debt = 25;
    } else if (debt_ratio < 30) {
        s_debt = 18;
    } else if (debt_ratio < 50) {
        s_debt = 10;
    } else if (debt_ratio < 70) {
        s_debt = 5;
    }

    int s_savings = 0;
    if (savings_rate >= 30) {
        s_savings = 25;
    } else if (savings_rate >= 15) {
        s_savings = 18;
    } else if (savings_rate >= 5) {
        s_savings = 10;
    } else if (savings_rate >= 0) {
        s_savings = 5;
    }

    int s_discipline = 0;
    if (avg_income > 0 && avg_burn < avg_income) {
        s_discipline = 25;
    } else if (avg_income > 0 && avg_burn < avg_income * 1.1) {
        s_discipline = 12;
    } else if (avg_income <= 0 && avg_burn < 1000) {
        s_discipline = 10;
    }

    int total = s_liquidity + s_debt + s_savings + s_discipline;
    if (total > 100) {
        total = 100;
    }
    const char* grade = "D";
    if (total >= 85) {
        grade = "A";
    } else if (total >= 70) {
        grade = "B";
    } else if (total >= 50) {
        grade = "C";
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "score_liquidity", (double)s_liquidity);
    csilk_json_add_number(out, "score_debt", (double)s_debt);
    csilk_json_add_number(out, "score_savings", (double)s_savings);
    csilk_json_add_number(out, "score_discipline", (double)s_discipline);
    csilk_json_add_number(out, "total_score", (double)total);
    csilk_json_add_string(out, "grade", grade);
    size_t sl2 = 0;
    char*  s = csilk_json_serialize(out, &sl2);
    csilk_json_free(out);
    if (ctx) {
        csilk_json_free(ctx);
    }
    return s ? s : strdup("{}");
}

static char*
step_hs_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* ctx = NULL;
    if (ctx_json && ctx_json[0]) {
        ctx = csilk_json_parse(ctx_json);
    }
    if (!ctx) {
        return strdup("{\"markdown\":\"暂无健康分数据。\"}");
    }
    csilk_json_t* col = csilk_json_get(ctx, "hs_collect");
    csilk_json_t* sc = csilk_json_get(ctx, "hs_score");
    double        liquid = col ? db_get_num(col, "liquid_cash") : 0;
    double        assets = col ? db_get_num(col, "total_assets") : 0;
    double        debt = col ? db_get_num(col, "total_debt") : 0;
    double        burn = col ? db_get_num(col, "avg_burn") : 0;
    double        income = col ? db_get_num(col, "avg_income") : 0;
    double        sr = col ? db_get_num(col, "savings_rate") : 0;
    double        emer = col ? db_get_num(col, "emergency_months") : 0;
    double        dr = col ? db_get_num(col, "debt_ratio") : 0;
    int           total = sc ? (int)db_get_num(sc, "total_score") : 0;
    const char*   grade = sc ? csilk_json_get_string(sc, "grade") : "D";
    if (!grade) {
        grade = "D";
    }
    int sl = sc ? (int)db_get_num(sc, "score_liquidity") : 0;
    int sd = sc ? (int)db_get_num(sc, "score_debt") : 0;
    int sv = sc ? (int)db_get_num(sc, "score_savings") : 0;
    int sdi = sc ? (int)db_get_num(sc, "score_discipline") : 0;

    const char* label = "需改进";
    if (total >= 85) {
        label = "优秀";
    } else if (total >= 70) {
        label = "良好";
    } else if (total >= 50) {
        label = "一般";
    }

    char suggest[512];
    if (total >= 85) {
        snprintf(suggest, sizeof(suggest), "财务状况优秀，继续保持当前储蓄与支出节奏");
    } else if (emer < 3) {
        snprintf(
            suggest, sizeof(suggest), "优先补足应急资金至 3-6 个月支出（当前 %.1f 个月）", emer);
    } else if (dr >= 50) {
        snprintf(suggest, sizeof(suggest), "负债率偏高（%.0f%%），建议优先降低高息负债", dr);
    } else if (sr < 10) {
        snprintf(suggest, sizeof(suggest), "储蓄率偏低（%.0f%%），建议检视非必要支出", sr);
    } else {
        snprintf(suggest, sizeof(suggest), "整体良好，可优化短板维度进一步提升");
    }

    char xychart[2048];
    snprintf(xychart,
             sizeof(xychart),
             "```mermaid\n"
             "xychart-beta\n"
             "    title \"财务健康分项（满分25）\"\n"
             "    x-axis [\"流动性\",\"负债\",\"储蓄\",\"纪律\"]\n"
             "    y-axis \"分数\" 0 --> 25\n"
             "    bar [%d,%d,%d,%d]\n"
             "```\n",
             sl,
             sd,
             sv,
             sdi);

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 🏥 财务健康分报告\n\n"
             "**综合得分** `%d/100` ｜ **等级** `%s` ｜ **评价** `%s`\n\n"
             "%s\n"
             "| 维度 | 得分 | 满分 | 说明 |\n"
             "| :--- | :--- | :--- | :--- |\n"
             "| 流动性 | %d | 25 | 应急 %.1f 个月 |\n"
             "| 负债 | %d | 25 | 负债率 %.0f%% |\n"
             "| 储蓄 | %d | 25 | 储蓄率 %.0f%% |\n"
             "| 纪律 | %d | 25 | 收支 %s |\n"
             "| **合计** | **%d** | **100** | **%s** |\n\n"
             "**资产** `￥%.0f` ｜ **负债** `￥%.0f` ｜ **流动现金** `￥%.0f` ｜ "
             "**月均收入** `￥%.0f` ｜ **月均支出** `￥%.0f`\n\n"
             "#### 建议\n"
             "1. %s；\n"
             "2. 每月复盘时关注最低分的维度；\n"
             "3. 目标 3 个月内提升至 %d 分以上。\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"health_score\",\n"
             "  \"total_score\": %d,\n"
             "  \"grade\": \"%s\"\n"
             "}\n"
             "```\n",
             total,
             grade,
             label,
             xychart,
             sl,
             emer,
             sd,
             dr,
             sv,
             sr,
             sdi,
             burn < income ? "盈余" : "赤字",
             total,
             label,
             assets,
             debt,
             liquid,
             income,
             burn,
             suggest,
             total >= 85 ? 90 : total + 10,
             total,
             grade);
    csilk_json_free(ctx);
    return strdup(buf);
}

static const ai_workflow_graph_t g_hs_graph = {
    .id = "wf_health_score",
    .title = "财务健康分评估",
    .description =
        "从流动性/负债/储蓄/纪律四维度计算 0-100 健康分，定位短板并给出 3 个月提升路径。",
    .icon = "ph:heart",
    .node_count = 3,
    .nodes =
        {
                {"hs_collect", "财务数据汇聚", "汇聚资产负债与月均收支", step_hs_collect},
                {"hs_score", "四维度评分", "流动性/负债/储蓄/纪律加权评分", step_hs_score},
                {"generate_report",
             "健康分报告与提升建议",
             "生成健康分雷达图与短板改进方案",
             step_hs_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_health_score_get_graph(void)
{
    return &g_hs_graph;
}

/* ========================================================================= */
/*  Workflow: Emergency Fund (wf_emergency_fund)                             */
/* ========================================================================= */

static char*
step_ef_collect(csilk_db_pool_t*    pool,
                int64_t             user_id,
                const csilk_json_t* params,
                const char*         ctx_json)
{
    (void)ctx_json;
    if (!pool) {
        return strdup("{\"error\":\"db not ready\"}");
    }
    double liquid_cash = 0;
    {
        int64_t       tot = 0;
        csilk_json_t* arr = asset_list(pool, user_id, 1, 100, NULL, &tot);
        if (arr && csilk_json_is_array(arr)) {
            size_t n = csilk_json_array_size(arr);
            for (size_t i = 0; i < n; i++) {
                const csilk_json_t* it = csilk_json_array_get(arr, i);
                const char*         atype = csilk_json_get_string(it, "asset_type");
                if (!atype) {
                    atype = "";
                }
                if (strcmp(atype, "cash") != 0 && strcmp(atype, "bank") != 0 &&
                    strcmp(atype, "other_asset") != 0) {
                    continue;
                }
                const char* pid = csilk_json_get_string(it, "parent_id");
                if (pid && pid[0] && strcmp(pid, "0") != 0) {
                    continue;
                }
                liquid_cash += fh_get_asset_val(it);
            }
        }
        if (arr) {
            csilk_json_free(arr);
        }
    }
    double monthly_burn = fh_get_user_avg_monthly_burn(pool, user_id);
    int    target_months = 6;
    if (params) {
        double tv = db_get_num(params, "target_months");
        if (tv >= 1 && tv <= 12) {
            target_months = (int)tv;
        }
    }
    double target_amount = monthly_burn > 0 ? monthly_burn * target_months : 0;
    double current_runway = monthly_burn > 0 ? liquid_cash / monthly_burn : 0;
    double gap = target_amount > liquid_cash ? target_amount - liquid_cash : 0;

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "liquid_cash", liquid_cash);
    csilk_json_add_number(out, "monthly_burn", monthly_burn);
    csilk_json_add_number(out, "current_runway", current_runway);
    csilk_json_add_number(out, "target_months", (double)target_months);
    csilk_json_add_number(out, "target_amount", target_amount);
    csilk_json_add_number(out, "gap", gap);
    if (monthly_burn <= 0) {
        csilk_json_add_string(out, "note", "暂无月均支出数据，备用金目标按 0 估算");
    }
    size_t slen = 0;
    char*  s = csilk_json_serialize(out, &slen);
    csilk_json_free(out);
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

static char*
step_ef_health(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "ef_collect") : NULL;
    double        liquid = s0 ? db_get_num(s0, "liquid_cash") : 0;
    double        burn = s0 ? db_get_num(s0, "monthly_burn") : 0;
    double        target_amount = s0 ? db_get_num(s0, "target_amount") : 0;
    double        gap = s0 ? db_get_num(s0, "gap") : 0;
    double        runway = s0 ? db_get_num(s0, "current_runway") : 0;
    double        target_months = s0 ? db_get_num(s0, "target_months") : 6;

    double health_score = 0;
    if (target_amount > 0) {
        health_score = liquid / target_amount * 100.0;
        if (health_score > 100) {
            health_score = 100;
        }
        if (health_score < 0) {
            health_score = 0;
        }
    } else if (liquid > 0) {
        health_score = 100;
    }

    const char* level = "healthy";
    const char* level_label = "✅ 健康";
    if (target_amount <= 0) {
        level = "unknown";
        level_label = "ℹ️ 待补充数据";
    } else if (health_score >= 100) {
        level = "healthy";
        level_label = "✅ 健康";
    } else if (health_score >= 50) {
        level = "warning";
        level_label = "⚠️ 偏低";
    } else {
        level = "danger";
        level_label = "🔴 不足";
    }

    int    plan_months = 6;
    double monthly_topup = 0;
    if (gap > 0) {
        monthly_topup = gap / plan_months;
    }

    csilk_json_t* out = csilk_json_object();
    csilk_json_add_number(out, "liquid_cash", liquid);
    csilk_json_add_number(out, "monthly_burn", burn);
    csilk_json_add_number(out, "target_months", target_months);
    csilk_json_add_number(out, "target_amount", target_amount);
    csilk_json_add_number(out, "gap", gap);
    csilk_json_add_number(out, "current_runway", runway);
    csilk_json_add_number(out, "health_score", health_score);
    csilk_json_add_string(out, "level", level);
    csilk_json_add_string(out, "level_label", level_label);
    csilk_json_add_number(out, "plan_months", (double)plan_months);
    csilk_json_add_number(out, "monthly_topup", monthly_topup);
    size_t slen = 0;
    char*  s = csilk_json_serialize(out, &slen);
    csilk_json_free(out);
    if (root) {
        csilk_json_free(root);
    }
    char* ret = s ? strdup(s) : strdup("{}");
    free(s);
    return ret;
}

static char*
step_ef_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "ef_collect") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "ef_health") : NULL;
    double liquid = s1 ? db_get_num(s1, "liquid_cash") : (s0 ? db_get_num(s0, "liquid_cash") : 0);
    double burn = s1 ? db_get_num(s1, "monthly_burn") : (s0 ? db_get_num(s0, "monthly_burn") : 0);
    double target_amount = s1 ? db_get_num(s1, "target_amount") : 0;
    double gap = s1 ? db_get_num(s1, "gap") : 0;
    double runway = s1 ? db_get_num(s1, "current_runway") : 0;
    double health_score = s1 ? db_get_num(s1, "health_score") : 0;
    const char* level_label = s1 ? csilk_json_get_string(s1, "level_label") : "—";
    if (!level_label) {
        level_label = "—";
    }
    double target_months = s1 ? db_get_num(s1, "target_months") : 6;
    double monthly_topup = s1 ? db_get_num(s1, "monthly_topup") : 0;
    int    plan_months = s1 ? (int)db_get_num(s1, "plan_months") : 6;

    char mermaid[2048] = {0};
    if (target_amount > 0) {
        double filled = liquid > target_amount ? target_amount : liquid;
        double missing = gap;
        if (filled == 0 && missing == 0) {
            snprintf(
                mermaid, sizeof(mermaid), "> ℹ️ 暂无备用金目标数据，完成一次收支记账后重试。\n\n");
        } else {
            snprintf(mermaid,
                     sizeof(mermaid),
                     "```mermaid\npie showData\n    title 应急备用金达成度（目标 %.0f 个月）\n"
                     "    \"已备金额\" : %.2f\n"
                     "    \"缺口金额\" : %.2f\n```\n\n",
                     target_months,
                     filled,
                     missing);
        }
    } else {
        snprintf(mermaid,
                 sizeof(mermaid),
                 "> ℹ️ 月均支出为 0，无法计算备用金目标。建议先完成收支记账。\n\n");
    }

    char advice[1024] = {0};
    if (target_amount <= 0) {
        snprintf(advice,
                 sizeof(advice),
                 "暂无月均支出基线，建议先记录 1-2 个月日常流水后再评估备用金。");
    } else if (health_score >= 100) {
        snprintf(
            advice,
            sizeof(advice),
            "备用金已达标（覆盖 %.1f 个月），建议将多余流动资金转入定投或稳健理财，避免现金闲置。",
            runway);
    } else if (health_score >= 50) {
        snprintf(advice,
                 sizeof(advice),
                 "备用金偏低（%.0f%%），建议未来 %d 个月每月追加 `￥%.0f` 补齐缺口 `￥%.0f`。",
                 health_score,
                 plan_months,
                 monthly_topup,
                 gap);
    } else {
        snprintf(advice,
                 sizeof(advice),
                 "备用金不足（%.0f%%，仅覆盖 %.1f 个月），建议优先暂停非刚需支出，%d "
                 "个月内每月追加 `￥%.0f` 紧急补足。",
                 health_score,
                 runway,
                 plan_months,
                 monthly_topup);
    }

    char buf[16384];
    snprintf(buf,
             sizeof(buf),
             "### 🛡️ 应急基金健康检查报告\n\n"
             "**健康度** `%.0f 分` ｜ **状态** `%s` ｜ **当前覆盖** `%.1f 个月` ｜ **目标** `%.0f "
             "个月`\n\n"
             "**流动现金** `￥%.2f` ｜ **月均刚性支出** `￥%.2f` ｜ **备用金目标** `￥%.2f` ｜ "
             "**缺口** `￥%.2f`\n\n"
             "#### 一、达成度构成\n"
             "%s"
             "#### 二、补足计划\n"
             "| 指标 | 数值 |\n"
             "| :--- | :--- |\n"
             "| 目标月数 | %.0f 个月 |\n"
             "| 目标金额 | ￥%.2f |\n"
             "| 当前流动现金 | ￥%.2f |\n"
             "| 缺口 | ￥%.2f |\n"
             "| 建议补足周期 | %d 个月 |\n"
             "| 建议每月追加 | ￥%.2f |\n"
             "\n"
             "#### 三、处置建议\n"
             "%s\n\n"
             "- **健康 (≥100%%)**：已达标，超出部分可转定投；\n"
             "- **偏低 (50-100%%)**：按月追加，优先保障 3 个月底线；\n"
             "- **不足 (<50%%)**：暂停大额非刚需消费，优先补足至 3 个月。\n"
             "```action\n"
             "{\n"
             "  \"action_type\": \"emergency_fund_check\",\n"
             "  \"health_score\": %.0f,\n"
             "  \"gap\": %.2f,\n"
             "  \"monthly_topup\": %.2f,\n"
             "  \"target_months\": %.0f\n"
             "}\n"
             "```\n",
             health_score,
             level_label,
             runway,
             target_months,
             liquid,
             burn,
             target_amount,
             gap,
             mermaid,
             target_months,
             target_amount,
             liquid,
             gap,
             plan_months,
             monthly_topup,
             advice,
             health_score,
             gap,
             monthly_topup,
             target_months);

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_ef_graph = {
    .id = "wf_emergency_fund",
    .title = "应急基金健康检查",
    .description = "盘点流动现金与月均刚性支出，评分备用金健康度并输出缺口补足计划。",
    .icon = "ph:shield-check",
    .node_count = 3,
    .nodes =
        {
                {"ef_collect",
             "流动现金与月均支出盘点",
             "统计活期现金与月均刚性支出基线",
             step_ef_collect},
                {"ef_health", "健康度评分与缺口测算", "计算覆盖月数、健康度与补足缺口", step_ef_health},
                {"generate_report",
             "健康仪表盘与补足计划",
             "生成达成度饼图、补足计划表与处置建议",
             step_ef_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_emergency_fund_get_graph(void)
{
    return &g_ef_graph;
}
