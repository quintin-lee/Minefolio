#pragma once

#include "csilk/csilk.h"
#include "application/ai/dtos.h"

#ifdef __cplusplus
extern "C" {
#endif

int ai_usecase_test_connection(const csilk_json_t*  body,
                               csilk_json_t**       out_resp,
                               ai_usecase_result_t* out_res);

int ai_usecase_fetch_models(const csilk_json_t*  body,
                            csilk_json_t**       out_resp,
                            ai_usecase_result_t* out_res);

#ifdef __cplusplus
}
#endif
