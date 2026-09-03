#pragma once

#include "csilk/csilk.h"
#include "application/market/commands.h"
#include "application/market/dtos.h"

int market_usecase_search(const search_market_cmd_t* cmd,
                          csilk_json_t**             out_list,
                          market_usecase_result_t*   out_res);

int market_usecase_quote(const fetch_quote_cmd_t* cmd,
                         csilk_json_t**           out_quote,
                         market_usecase_result_t* out_res);

int market_usecase_sync_all(void*                    pool,
                            int64_t                  user_id,
                            int*                     out_synced,
                            int*                     out_failed,
                            market_usecase_result_t* out_res);

int market_usecase_sync_single(void*                    pool,
                               int64_t                  user_id,
                               int64_t                  asset_id,
                               csilk_json_t**           out_res_data,
                               market_usecase_result_t* out_res);

int market_usecase_price_history(void*                    pool,
                                 int64_t                  user_id,
                                 int64_t                  asset_id,
                                 int                      limit,
                                 csilk_json_t**           out_rows,
                                 market_usecase_result_t* out_res);

int market_usecase_get_settings(csilk_json_t** out_settings, market_usecase_result_t* out_res);

int market_usecase_update_settings(const update_market_settings_cmd_t* cmd,
                                   market_usecase_result_t*            out_res);

int market_usecase_test_proxy(const char*              proxy,
                              csilk_json_t**           out_data,
                              market_usecase_result_t* out_res);

int market_usecase_get_exchange_rates(csilk_json_t** out_rates, market_usecase_result_t* out_res);

int market_usecase_update_exchange_rate(void*                             pool,
                                        const update_exchange_rate_cmd_t* cmd,
                                        market_usecase_result_t*          out_res);

int market_usecase_get_fx_history(const char*              currency,
                                  int                      days,
                                  csilk_json_t**           out_history,
                                  market_usecase_result_t* out_res);

int market_usecase_do_sync_user(void* pool, int64_t user_id, int* out_synced, int* out_failed);
