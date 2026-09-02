#include "services/ai/workflows/portfolio_analysis.h"
#include "repositories/asset_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static char*
step_pr_scan(csilk_db_pool_t*    pool,
             int64_t             user_id,
             const csilk_json_t* params,
             const char*         ctx_json)
{
    (void)params;
    (void)ctx_json;
    int64_t       total = 0;
    csilk_json_t* list = asset_list(pool, user_id, 1, 100, NULL, &total);
    double        stock_val = 0.0, fund_val = 0.0, crypto_val = 0.0, bond_val = 0.0, cash_val = 0.0;
    csilk_json_t* top_holdings = csilk_json_array();

    if (list) {
        size_t n = csilk_json_array_size(list);
        for (size_t i = 0; i < n; i++) {
            csilk_json_t* a = csilk_json_array_get(list, i);
            const char*   type = csilk_json_get_string(a, "asset_type") ?: "";
            const char*   name = csilk_json_get_string(a, "name") ?: "未命名资产";
            double        val = db_get_num(a, "current_value");
            if (val == 0.0) {
                val = db_get_num(a, "balance");
            }

            if (strcmp(type, "stock") == 0) {
                stock_val += val;
            } else if (strcmp(type, "fund") == 0) {
                fund_val += val;
            } else if (strcmp(type, "crypto") == 0) {
                crypto_val += val;
            } else if (strcmp(type, "bond") == 0) {
                bond_val += val;
            } else if (strcmp(type, "cash") == 0 || strcmp(type, "bank") == 0) {
                cash_val += val;
            }

            if (val > 0.0 && (strcmp(type, "stock") == 0 || strcmp(type, "fund") == 0 ||
                              strcmp(type, "crypto") == 0 || strcmp(type, "bond") == 0)) {
                csilk_json_t* h = csilk_json_object();
                csilk_json_add_string(h, "name", name);
                csilk_json_add_string(h, "type", type);
                csilk_json_add_number(h, "current_value", val);
                csilk_json_add_number(h, "cost_basis", db_get_num(a, "cost_basis"));
                csilk_json_add_number(h, "quantity", db_get_num(a, "quantity"));
                csilk_json_array_append(top_holdings, h);
            }
        }
        csilk_json_free(list);
    }

    double total_val = stock_val + fund_val + crypto_val + bond_val + cash_val;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "total_portfolio_value", total_val);
    csilk_json_add_number(res, "stock_value", stock_val);
    csilk_json_add_number(res, "fund_value", fund_val);
    csilk_json_add_number(res, "crypto_value", crypto_val);
    csilk_json_add_number(res, "bond_value", bond_val);
    csilk_json_add_number(res, "cash_value", cash_val);
    csilk_json_add_array(res, "holdings", top_holdings);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_pr_exposure(csilk_db_pool_t*    pool,
                 int64_t             user_id,
                 const csilk_json_t* params,
                 const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "scan_holdings") : NULL;

    double total = s0 ? db_get_num(s0, "total_portfolio_value") : 0.0;
    double stock = s0 ? db_get_num(s0, "stock_value") : 0.0;
    double fund = s0 ? db_get_num(s0, "fund_value") : 0.0;
    double crypto = s0 ? db_get_num(s0, "crypto_value") : 0.0;
    double bond = s0 ? db_get_num(s0, "bond_value") : 0.0;
    double cash = s0 ? db_get_num(s0, "cash_value") : 0.0;
    if (root) {
        csilk_json_free(root);
    }

    double equity_val = stock + fund + crypto;
    double fixed_val = bond;
    double cash_val = cash;

    double equity_pct = (total > 0.0) ? (equity_val / total) * 100.0 : 0.0;
    double fixed_pct = (total > 0.0) ? (fixed_val / total) * 100.0 : 0.0;
    double cash_pct = (total > 0.0) ? (cash_val / total) * 100.0 : 0.0;

    double equity_deviation = (total > 0.0) ? (equity_pct - 50.0) : 0.0;
    double fixed_deviation = (total > 0.0) ? (fixed_pct - 30.0) : 0.0;
    double cash_deviation = (total > 0.0) ? (cash_pct - 20.0) : 0.0;

    csilk_json_t* res = csilk_json_object();
    csilk_json_add_number(res, "equity_pct", equity_pct);
    csilk_json_add_number(res, "fixed_pct", fixed_pct);
    csilk_json_add_number(res, "cash_pct", cash_pct);
    csilk_json_add_number(res, "equity_deviation", equity_deviation);
    csilk_json_add_number(res, "fixed_deviation", fixed_deviation);
    csilk_json_add_number(res, "cash_deviation", cash_deviation);

    size_t len = 0;
    char*  s = csilk_json_serialize(res, &len);
    csilk_json_free(res);
    return s;
}

static char*
step_pr_report(csilk_db_pool_t*    pool,
               int64_t             user_id,
               const csilk_json_t* params,
               const char*         ctx_json)
{
    (void)pool;
    (void)user_id;
    (void)params;
    csilk_json_t* root = ctx_json ? csilk_json_parse(ctx_json) : NULL;
    csilk_json_t* s0 = root ? csilk_json_get(root, "scan_holdings") : NULL;
    csilk_json_t* s1 = root ? csilk_json_get(root, "risk_exposure") : NULL;

    double total = s0 ? db_get_num(s0, "total_portfolio_value") : 0.0;
    double stock = s0 ? db_get_num(s0, "stock_value") : 0.0;
    double fund = s0 ? db_get_num(s0, "fund_value") : 0.0;
    double crypto = s0 ? db_get_num(s0, "crypto_value") : 0.0;
    double bond = s0 ? db_get_num(s0, "bond_value") : 0.0;
    double cash = s0 ? db_get_num(s0, "cash_value") : 0.0;

    double equity_pct = s1 ? db_get_num(s1, "equity_pct") : 0.0;
    double fixed_pct = s1 ? db_get_num(s1, "fixed_pct") : 0.0;
    double cash_pct = s1 ? db_get_num(s1, "cash_pct") : 0.0;
    double eq_dev = s1 ? db_get_num(s1, "equity_deviation") : 0.0;

    char mermaid_pie[2048] = {0};
    if (total > 0.0) {
        snprintf(mermaid_pie,
                 sizeof(mermaid_pie),
                 "```mermaid\n"
                 "pie showData\n"
                 "    title 实际投资资产大类配置分布\n");
        if (stock > 0.0) {
            char l[128];
            snprintf(
                l, sizeof(l), "    \"股票证券 (%.1f%%)\" : %.2f\n", (stock / total) * 100.0, stock);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        if (fund > 0.0) {
            char l[128];
            snprintf(
                l, sizeof(l), "    \"基金理财 (%.1f%%)\" : %.2f\n", (fund / total) * 100.0, fund);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        if (crypto > 0.0) {
            char l[128];
            snprintf(l,
                     sizeof(l),
                     "    \"数字货币 (%.1f%%)\" : %.2f\n",
                     (crypto / total) * 100.0,
                     crypto);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        if (bond > 0.0) {
            char l[128];
            snprintf(
                l, sizeof(l), "    \"债券固收 (%.1f%%)\" : %.2f\n", (bond / total) * 100.0, bond);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        if (cash > 0.0) {
            char l[128];
            snprintf(
                l, sizeof(l), "    \"现金存款 (%.1f%%)\" : %.2f\n", (cash / total) * 100.0, cash);
            strncat(mermaid_pie, l, sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
        }
        strncat(mermaid_pie, "```\n\n", sizeof(mermaid_pie) - strlen(mermaid_pie) - 1);
    } else {
        snprintf(
            mermaid_pie, sizeof(mermaid_pie), "> 💡 **提示**：当前尚未添加投资或现金类资产。\n\n");
    }

    char buf[8192];
    snprintf(
        buf,
        sizeof(buf),
        "### 📈 投资组合大类资产配置与再平衡报告\n\n"
        "**总资产组合规模**：`￥%.2f`\n\n"
        "#### 一、大类资产敞口与偏离度（真实数据）\n"
        "- 📊 **权益类资产（股票/基金/加密货币）**：`￥%.2f` (占比 **%.1f%%**，基准 "
        "50.0%%，偏离度：%+.1f%%)\n"
        "- 🛡️ **固收及债券资产**：`￥%.2f` (占比 **%.1f%%**，基准 30.0%%)\n"
        "- 💵 **流动性现金及存款**：`￥%.2f` (占比 **%.1f%%**，基准 20.0%%)\n\n"
        "#### 二、资产配置可视化图表\n"
        "%s"
        "#### 三、再平衡操作与风控建议\n"
        "%s\n\n"
        "1. "
        "**定投平衡法**"
        "：对于偏离基准的大类，优先通过后续月度现金流结余定投补充低配资产，避免频繁调仓产生额外摩擦"
        "成本；\n"
        "2. **流动性纪律**：无论市场行情如何变动，建议保留至少 15%%~20%% "
        "的现金及等价物作为防守底仓。",
        total,
        (stock + fund + crypto),
        equity_pct,
        eq_dev,
        bond,
        fixed_pct,
        cash,
        cash_pct,
        mermaid_pie,
        total <= 0.0
            ? "当前暂无资产数据，请先在「资产管理」中添加账户或持仓。"
            : (fabs(eq_dev) > 15.0
                   ? (eq_dev > 0
                          ? "⚠️ **权益类超配警报**：权益类资产偏离基准超过 "
                            "+15%"
                            "，组合波动风险较大。建议逐步止盈部分高估值标的，将资金再平衡至稳健固收"
                            "类。"
                          : "🟡 **权益类低配提示**：权益类资产偏离基准超过 "
                            "-15%"
                            "，防御性过强。可逢低逐步增配优质宽基指数或核心资产提升长期复合回报。")
                   : "✅ **资产配置健康稳健**：当前各大类资产敞口均处于合理波动容忍区间（±15% "
                     "内），无需进行激进调仓。"));

    if (root) {
        csilk_json_free(root);
    }
    return strdup(buf);
}

static const ai_workflow_graph_t g_pr_graph = {
    .id = "wf_portfolio_rebalance",
    .title = "投资组合再平衡体检",
    .description =
        "扫描股票/基金/债券/加密资产全局持仓，计算大类资产配置偏离度，输出再平衡调仓建议与草案。",
    .icon = "ph:chart-polar",
    .node_count = 3,
    .nodes =
        {
                {"scan_holdings",
             "全局持仓与资产扫描",
             "统计权益、固收、现金各类资产最新市值与比重",
             step_pr_scan},
                {"risk_exposure",
             "大类资产敞口测算",
             "比对标准大类配置基准，计算偏离幅度",
             step_pr_exposure},
                {"generate_report",
             "调仓方案与图表生成",
             "生成资产配置结构图与再平衡执行方案",
             step_pr_report},
                },
};

const ai_workflow_graph_t*
ai_workflow_portfolio_rebalance_get_graph(void)
{
    return &g_pr_graph;
}
