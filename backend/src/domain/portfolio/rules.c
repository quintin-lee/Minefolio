#include "domain/portfolio/rules.h"
#include "core/financial/percentage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief 单批次买入留下的成本批次（FIFO 重放用） */
typedef struct {
    quantity_t qty;           /**< 批次剩余份额 */
    price_t    cost_per_unit; /**< 含手续费的批次单位成本 */
} pnl_lot_t;

typedef struct {
    int64_t    asset_id;
    money_t    cost_for_pnl;
    quantity_t qty;
    money_t    realized;
    bool       fifo; /**< 该标的是否采用 FIFO 成本法 */
    pnl_lot_t* lots; /**< FIFO 批次队列（按买入时序） */
    size_t     lot_count;
    size_t     lot_cap;
} pnl_acc_t;

static int
pnl_acc_push_lot(pnl_acc_t* acc, quantity_t qty, money_t total_cost)
{
    if (!quantity_is_positive(qty)) {
        return 0;
    }
    if (acc->lot_count == acc->lot_cap) {
        size_t     new_cap = acc->lot_cap ? acc->lot_cap * 2 : 8;
        pnl_lot_t* grown = (pnl_lot_t*)realloc(acc->lots, new_cap * sizeof(pnl_lot_t));
        if (!grown) {
            return -1;
        }
        acc->lots = grown;
        acc->lot_cap = new_cap;
    }
    price_t cpu;
    if (money_div_quantity(total_cost, qty, 8, ROUND_HALF_UP, &cpu) != DECIMAL_OK) {
        return -1;
    }
    acc->lots[acc->lot_count].qty = qty;
    acc->lots[acc->lot_count].cost_per_unit = cpu;
    acc->lot_count++;
    return 0;
}

/**
 * @brief 按 FIFO 顺序扣减批次成本
 * @return 0 成功；1 批次不足（调用方回退到加权平均）；-1 算术失败
 */
static int
pnl_acc_consume_fifo(pnl_acc_t* acc, quantity_t sell_qty, money_t* out_cost)
{
    quantity_t lot_total = quantity_zero();
    for (size_t i = 0; i < acc->lot_count; i++) {
        if (quantity_add(lot_total, acc->lots[i].qty, &lot_total) != DECIMAL_OK) {
            return -1;
        }
    }
    if (quantity_cmp(lot_total, sell_qty) < 0) {
        return 1;
    }

    money_t    sold_cost = money_zero(acc->cost_for_pnl.currency);
    quantity_t remaining = sell_qty;
    for (size_t i = 0; i < acc->lot_count && quantity_is_positive(remaining); i++) {
        pnl_lot_t* lot = &acc->lots[i];
        if (!quantity_is_positive(lot->qty)) {
            continue;
        }
        quantity_t deduct = quantity_cmp(lot->qty, remaining) <= 0 ? lot->qty : remaining;
        money_t    lot_cost;
        if (price_times_quantity(lot->cost_per_unit, deduct, &lot_cost) != DECIMAL_OK ||
            money_add(sold_cost, lot_cost, &sold_cost) != DECIMAL_OK ||
            quantity_sub(lot->qty, deduct, &lot->qty) != DECIMAL_OK ||
            quantity_sub(remaining, deduct, &remaining) != DECIMAL_OK) {
            return -1;
        }
    }

    /* 压缩已耗尽的批次，保留仍有余量的批次顺序 */
    size_t w = 0;
    for (size_t i = 0; i < acc->lot_count; i++) {
        if (quantity_is_positive(acc->lots[i].qty)) {
            acc->lots[w++] = acc->lots[i];
        }
    }
    acc->lot_count = w;

    *out_cost = sold_cost;
    return 0;
}

int
mf_portfolio_rule_apply_trade_events(mf_holding_item_t*                items,
                                     size_t                            item_count,
                                     const mf_portfolio_trade_event_t* events,
                                     size_t                            event_count)
{
    if (!items && item_count > 0) {
        return -1;
    }
    if (item_count == 0) {
        return 0;
    }

    pnl_acc_t* accs = (pnl_acc_t*)calloc(item_count, sizeof(pnl_acc_t));
    if (!accs) {
        return -1;
    }
    int rc = 0;

    for (size_t i = 0; i < item_count; i++) {
        accs[i].asset_id = items[i].asset_id;
        accs[i].cost_for_pnl = money_zero(items[i].currency);
        accs[i].qty = quantity_zero();
        accs[i].realized = money_zero(items[i].currency);
        accs[i].fifo = (strcmp(items[i].cost_method, "fifo") == 0);
        accs[i].lots = NULL;
        accs[i].lot_count = 0;
        accs[i].lot_cap = 0;
    }

    if (events && event_count > 0) {
        for (size_t i = 0; i < event_count; i++) {
            const mf_portfolio_trade_event_t* ev = &events[i];
            if (!ev->type[0]) {
                continue;
            }
            if (strcmp(ev->type, "buy") != 0 && strcmp(ev->type, "sell") != 0 &&
                strcmp(ev->type, "income") != 0 && strcmp(ev->type, "dividend") != 0 &&
                strcmp(ev->type, "interest") != 0 && strcmp(ev->type, "fee") != 0) {
                continue;
            }

            int found_idx = -1;
            for (size_t j = 0; j < item_count; j++) {
                if (accs[j].asset_id == ev->asset_id) {
                    found_idx = (int)j;
                    break;
                }
            }
            if (found_idx < 0) {
                continue;
            }

            pnl_acc_t* acc = &accs[found_idx];

            if (strcmp(ev->type, "buy") == 0) {
                /* 买入成本 = 成交额 + 手续费，与写路径 ledger/performance 口径一致 */
                money_t buy_cost = ev->amount;
                if (money_add(buy_cost, ev->fee, &buy_cost) != DECIMAL_OK ||
                    money_add(acc->cost_for_pnl, buy_cost, &acc->cost_for_pnl) != DECIMAL_OK ||
                    quantity_add(acc->qty, ev->quantity, &acc->qty) != DECIMAL_OK) {
                    rc = -1;
                    goto cleanup;
                }
                if (acc->fifo && pnl_acc_push_lot(acc, ev->quantity, buy_cost) != 0) {
                    rc = -1;
                    goto cleanup;
                }
            } else if (strcmp(ev->type, "sell") == 0) {
                if (quantity_is_positive(acc->qty)) {
                    money_t sold_cost = money_zero(acc->cost_for_pnl.currency);
                    bool    have_cost = false;

                    /* FIFO 成本法：按批次先进先出扣减，与写路径
                     * ledger_calc_sell_position_fifo 保持同一口径 */
                    if (acc->fifo && acc->lot_count > 0) {
                        int frc = pnl_acc_consume_fifo(acc, ev->quantity, &sold_cost);
                        if (frc < 0) {
                            rc = -1;
                            goto cleanup;
                        }
                        have_cost = (frc == 0);
                    }

                    if (!have_cost) {
                        /* 加权平均：批次缺失（历史数据）或非 FIFO 成本法 */
                        if (quantity_cmp(ev->quantity, acc->qty) >= 0) {
                            sold_cost = acc->cost_for_pnl;
                        } else {
                            decimal_t ratio;
                            if (decimal_div(ev->quantity.units,
                                            acc->qty.units,
                                            12,
                                            ROUND_HALF_UP,
                                            &ratio) == DECIMAL_OK) {
                                decimal_t sold_cost_units;
                                if (decimal_mul(acc->cost_for_pnl.amount,
                                                ratio,
                                                &sold_cost_units) == DECIMAL_OK) {
                                    if (decimal_round(
                                            sold_cost_units,
                                            currency_precision(acc->cost_for_pnl.currency),
                                            ROUND_HALF_UP,
                                            &sold_cost.amount) != DECIMAL_OK) {
                                        sold_cost = money_zero(acc->cost_for_pnl.currency);
                                    }
                                }
                            }
                        }
                    }

                    /* 卖出净收入 = 成交额 - 手续费，再扣减成本 → 与写路径一致 */
                    money_t net_proceeds;
                    money_t trade_realized;
                    if (money_sub(ev->amount, ev->fee, &net_proceeds) != DECIMAL_OK ||
                        money_sub(net_proceeds, sold_cost, &trade_realized) != DECIMAL_OK ||
                        money_add(acc->realized, trade_realized, &acc->realized) != DECIMAL_OK) {
                        rc = -1;
                        goto cleanup;
                    }

                    /* 扣减份额与成本池（清仓时直接置零，彻底消除浮点微小残差） */
                    if (quantity_cmp(ev->quantity, acc->qty) >= 0) {
                        acc->qty = quantity_zero();
                        acc->cost_for_pnl = money_zero(acc->cost_for_pnl.currency);
                        acc->lot_count = 0;
                    } else {
                        if (quantity_sub(acc->qty, ev->quantity, &acc->qty) != DECIMAL_OK ||
                            money_sub(acc->cost_for_pnl, sold_cost, &acc->cost_for_pnl) !=
                                DECIMAL_OK) {
                            rc = -1;
                            goto cleanup;
                        }
                        if (money_is_negative(acc->cost_for_pnl)) {
                            acc->cost_for_pnl = money_zero(acc->cost_for_pnl.currency);
                        }
                    }
                }
            } else if (strcmp(ev->type, "income") == 0 || strcmp(ev->type, "dividend") == 0 ||
                       strcmp(ev->type, "interest") == 0) {
                if (money_add(acc->realized, ev->amount, &acc->realized) != DECIMAL_OK) {
                    rc = -1;
                    goto cleanup;
                }
            } else if (strcmp(ev->type, "fee") == 0) {
                /* 独立费用交易（无父交易）计为已实现亏损，与绩效口径一致 */
                if (money_sub(acc->realized, ev->fee, &acc->realized) != DECIMAL_OK) {
                    rc = -1;
                    goto cleanup;
                }
            }
        }
    }

    for (size_t i = 0; i < item_count; i++) {
        mf_holding_item_t* it = &items[i];
        money_t            market = {0};
        if (price_times_quantity(it->net_value, it->quantity, &market) == DECIMAL_OK) {
            it->market_value = market;
        } else {
            it->market_value = money_zero(it->currency);
        }

        if (quantity_is_zero(it->quantity)) {
            it->market_value = money_zero(it->currency);
            it->floating_pnl = money_zero(it->currency);
            it->floating_pct = 0.0;
        } else {
            if (money_sub(it->market_value, it->cost_basis, &it->floating_pnl) != DECIMAL_OK) {
                it->floating_pnl = money_zero(it->currency);
            }
            percentage_t pct;
            if (percentage_calc(it->floating_pnl, it->cost_basis, 4, ROUND_HALF_UP, &pct) ==
                DECIMAL_OK) {
                it->floating_pct = percentage_to_double(pct);
            } else {
                it->floating_pct = 0.0;
            }
        }
        it->realized_pnl = accs[i].realized;
    }

cleanup:
    for (size_t i = 0; i < item_count; i++) {
        free(accs[i].lots);
    }
    free(accs);
    return rc;
}

typedef struct {
    currency_t cur;
    money_t    market;
    money_t    cost;
    money_t    floating;
    money_t    realized;
} summary_currency_bucket_t;

int
mf_portfolio_rule_aggregate_summary(const mf_holding_item_t*  items,
                                    size_t                    item_count,
                                    currency_t                base_currency,
                                    const mf_fx_rate_table_t* fx_table,
                                    mf_portfolio_summary_t*   out_summary)
{
    if (!out_summary) {
        return -1;
    }
    memset(out_summary, 0, sizeof(*out_summary));

    out_summary->total_market_value = money_zero(base_currency);
    out_summary->total_cost_basis = money_zero(base_currency);
    out_summary->total_floating_pnl = money_zero(base_currency);
    out_summary->total_realized_pnl = money_zero(base_currency);
    out_summary->floating_pct = 0.0;

    if (!items || item_count == 0) {
        return 0;
    }

    /* 先按原币种累加后再做一次性汇率折算，避免逐笔独立折算四舍五入产生累加尾差。 */
    summary_currency_bucket_t* buckets =
        (summary_currency_bucket_t*)calloc(item_count, sizeof(summary_currency_bucket_t));
    if (!buckets) {
        return -1;
    }
    size_t bucket_count = 0;
    int    rc = 0;

    for (size_t i = 0; i < item_count; i++) {
        size_t found = bucket_count;
        for (size_t b = 0; b < bucket_count; b++) {
            if (currency_equals(buckets[b].cur, items[i].currency)) {
                found = b;
                break;
            }
        }
        if (found == bucket_count) {
            buckets[found].cur = items[i].currency;
            buckets[found].market = items[i].market_value;
            buckets[found].cost = items[i].cost_basis;
            buckets[found].floating = items[i].floating_pnl;
            buckets[found].realized = items[i].realized_pnl;
            bucket_count++;
        } else {
            if (money_add(buckets[found].market, items[i].market_value, &buckets[found].market) !=
                    DECIMAL_OK ||
                money_add(buckets[found].cost, items[i].cost_basis, &buckets[found].cost) !=
                    DECIMAL_OK ||
                money_add(buckets[found].floating,
                          items[i].floating_pnl,
                          &buckets[found].floating) != DECIMAL_OK ||
                money_add(buckets[found].realized,
                          items[i].realized_pnl,
                          &buckets[found].realized) != DECIMAL_OK) {
                rc = -1;
                goto cleanup;
            }
        }
    }

    for (size_t b = 0; b < bucket_count; b++) {
        money_t conv_market = buckets[b].market;
        money_t conv_cost = buckets[b].cost;
        money_t conv_floating = buckets[b].floating;
        money_t conv_realized = buckets[b].realized;

        if (!currency_equals(buckets[b].cur, base_currency)) {
            if (!fx_table) {
                // 币种不一致且无汇率表时严禁盲目相加
                rc = -1;
                goto cleanup;
            }
            if (mf_fx_convert_money(buckets[b].market, base_currency, fx_table, &conv_market) !=
                    0 ||
                mf_fx_convert_money(buckets[b].cost, base_currency, fx_table, &conv_cost) != 0 ||
                mf_fx_convert_money(buckets[b].floating, base_currency, fx_table, &conv_floating) !=
                    0 ||
                mf_fx_convert_money(buckets[b].realized, base_currency, fx_table, &conv_realized) !=
                    0) {
                rc = -1;
                goto cleanup;
            }
        }

        if (money_add(out_summary->total_market_value,
                      conv_market,
                      &out_summary->total_market_value) != DECIMAL_OK ||
            money_add(out_summary->total_cost_basis, conv_cost, &out_summary->total_cost_basis) !=
                DECIMAL_OK ||
            money_add(out_summary->total_floating_pnl,
                      conv_floating,
                      &out_summary->total_floating_pnl) != DECIMAL_OK ||
            money_add(out_summary->total_realized_pnl,
                      conv_realized,
                      &out_summary->total_realized_pnl) != DECIMAL_OK) {
            rc = -1;
            goto cleanup;
        }
    }

    percentage_t pct;
    if (percentage_calc(out_summary->total_floating_pnl,
                        out_summary->total_cost_basis,
                        4,
                        ROUND_HALF_UP,
                        &pct) == DECIMAL_OK) {
        out_summary->floating_pct = percentage_to_double(pct);
    } else {
        out_summary->floating_pct = 0.0;
    }

cleanup:
    free(buckets);
    if (rc != 0) {
        memset(out_summary, 0, sizeof(*out_summary));
        out_summary->total_market_value = money_zero(base_currency);
        out_summary->total_cost_basis = money_zero(base_currency);
        out_summary->total_floating_pnl = money_zero(base_currency);
        out_summary->total_realized_pnl = money_zero(base_currency);
    }
    return rc;
}
