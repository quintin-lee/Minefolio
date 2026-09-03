#include "domain/asset/rules.h"
#include <stdio.h>
#include <string.h>

int mf_asset_rule_validate(const mf_asset_t* asset, char* err_buf, size_t err_len) {
    if (!asset) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Asset entity is NULL");
        return -1;
    }
    if (asset->user_id <= 0) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Invalid user_id");
        return -1;
    }
    if (asset->name[0] == '\0') {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Asset name cannot be empty");
        return -1;
    }
    if (asset->category_id <= 0) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Invalid category_id");
        return -1;
    }

    if (quantity_is_negative(asset->quantity)) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Asset quantity cannot be negative");
        return -1;
    }
    if (money_is_negative(asset->cost_basis)) {
        if (err_buf && err_len) snprintf(err_buf, err_len, "Asset cost_basis cannot be negative");
        return -1;
    }

    return 0;
}

int mf_asset_rule_derive_investment_values(mf_asset_t* asset) {
    if (!asset) return -1;
    if (!mf_asset_is_investment(asset)) return 0;

    if (quantity_is_positive(asset->quantity) && decimal_is_positive(asset->net_value.unit_price)) {
        money_t market = {0};
        if (price_times_quantity(asset->net_value, asset->quantity, &market) == DECIMAL_OK) {
            asset->current_value = market;
            if (!money_is_positive(asset->cost_basis)) {
                asset->cost_basis = market;
            }
        }
    }
    return 0;
}

int mf_asset_rule_calculate_floating_pnl(const mf_asset_t* asset, money_t* out_pnl, double* out_pct) {
    if (!asset) return -1;

    money_t pnl = money_zero(asset->currency);
    double pct = 0.0;

    if (mf_asset_is_investment(asset)) {
        money_sub(asset->current_value, asset->cost_basis, &pnl);
        double cost_d = money_to_double(asset->cost_basis);
        double pnl_d = money_to_double(pnl);
        if (cost_d > 0.0) {
            pct = (pnl_d / cost_d) * 100.0;
        }
    }

    if (out_pnl) *out_pnl = pnl;
    if (out_pct) *out_pct = pct;
    return 0;
}
