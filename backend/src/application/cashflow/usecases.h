#pragma once

#include "csilk/csilk.h"
#include "application/cashflow/commands.h"
#include "application/cashflow/dtos.h"

int cashflow_usecase_list_schedules(void*                      pool,
                                    int64_t                    user_id,
                                    csilk_json_t**             out_list,
                                    cashflow_usecase_result_t* out_res);

int cashflow_usecase_get_schedule(void*                      pool,
                                  int64_t                    user_id,
                                  int64_t                    id,
                                  csilk_json_t**             out_item,
                                  cashflow_usecase_result_t* out_res);

int cashflow_usecase_create_schedule(void*                        pool,
                                     const create_cashflow_cmd_t* cmd,
                                     int64_t*                     out_id,
                                     cashflow_usecase_result_t*   out_res);

int cashflow_usecase_update_schedule(void*                        pool,
                                     const update_cashflow_cmd_t* cmd,
                                     cashflow_usecase_result_t*   out_res);

int cashflow_usecase_delete_schedule(void*                      pool,
                                     int64_t                    user_id,
                                     int64_t                    id,
                                     cashflow_usecase_result_t* out_res);

int cashflow_usecase_get_calendar(void*                       pool,
                                  const query_calendar_cmd_t* cmd,
                                  csilk_json_t**              out_resp,
                                  cashflow_usecase_result_t*  out_res);

int cashflow_usecase_confirm_income(void*                         pool,
                                    const confirm_cashflow_cmd_t* cmd,
                                    int64_t*                      out_tx_id,
                                    cashflow_usecase_result_t*    out_res);
