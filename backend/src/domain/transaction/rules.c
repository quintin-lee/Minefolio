#include "domain/transaction/rules.h"
#include <stdio.h>
#include <string.h>

int
mf_tx_rule_validate(const mf_transaction_t* tx, char* err_buf, size_t err_len)
{
    if (!tx) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Transaction entity is NULL");
        }
        return -1;
    }
    if (tx->user_id <= 0) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Invalid user_id");
        }
        return -1;
    }
    if (tx->type[0] == '\0') {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Transaction type cannot be empty");
        }
        return -1;
    }

    /* 数量与费用不可为负数 */
    if (quantity_is_negative(tx->amount)) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Transaction amount cannot be negative");
        }
        return -1;
    }
    if (money_is_negative(tx->fee)) {
        if (err_buf && err_len) {
            snprintf(err_buf, err_len, "Transaction fee cannot be negative");
        }
        return -1;
    }

    return 0;
}

int
mf_tx_rule_build_fee_child(const mf_transaction_t* parent, mf_transaction_t* out_fee)
{
    if (!parent || !out_fee) {
        return -1;
    }
    if (!money_is_positive(parent->fee)) {
        return -1;
    }
    if (parent->id <= 0) {
        return -1;
    }

    memset(out_fee, 0, sizeof(*out_fee));
    out_fee->user_id = parent->user_id;
    out_fee->asset_id = 0;              /* 手续费子单不绑定投资标的 */
    out_fee->account_id = parent->account_id;
    out_fee->parent_tx_id = parent->id; /* 严格绑定父交易 ID */
    snprintf(out_fee->type, sizeof(out_fee->type), "fee");

    quantity_from_double(1.0, 4, &out_fee->amount);
    out_fee->price = price_from_decimal(parent->fee.amount, parent->fee.currency);
    out_fee->fee = money_zero(parent->fee.currency);
    snprintf(out_fee->fee_currency, sizeof(out_fee->fee_currency), "%s", parent->fee_currency);

    /* 必须保证 note 包含 'fee'，以便兼容原测试断言与审计追踪 */
    if (parent->note[0]) {
        if (strstr(parent->note, "fee") != NULL || strstr(parent->note, "Fee") != NULL) {
            snprintf(out_fee->note, sizeof(out_fee->note), "%s", parent->note);
        } else {
            snprintf(out_fee->note, sizeof(out_fee->note), "%.250s fee", parent->note);
        }
    } else {
        snprintf(out_fee->note, sizeof(out_fee->note), "fee");
    }

    if (parent->tx_time[0]) {
        snprintf(out_fee->tx_time, sizeof(out_fee->tx_time), "%s", parent->tx_time);
    }

    return 0;
}
