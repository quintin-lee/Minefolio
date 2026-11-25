#pragma once
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Validate the X-Gateway-Token header against the configured service token.
 * Returns 1 on success; on failure responds with code 1001 and returns 0. */
int gateway_auth_check(csilk_ctx_t* c);

#ifdef __cplusplus
}
#endif
