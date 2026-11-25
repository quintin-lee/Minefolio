#include "middlewares/gateway_auth.h"

#include "common/response.h"
#include "config/secret.h"
#include "csilk/csilk.h"

#include <string.h>

int
gateway_auth_check(csilk_ctx_t* c)
{
    char        expected[256];
    const char* cfg = config_secret_get("GATEWAY_TOKEN", expected, sizeof(expected));
    if (cfg == NULL || cfg[0] == '\0') {
        respond_error(c, 1001, "网关未启用");
        return 0;
    }

    const char* got = csilk_get_header(c, "X-Gateway-Token");
    if (got == NULL) {
        respond_error(c, 1001, "网关鉴权失败");
        return 0;
    }

    size_t                e = strlen(cfg);
    size_t                g = strlen(got);
    volatile unsigned int diff = (e == g) ? 0U : 1U;
    for (size_t i = 0; i < e && i < g; ++i) {
        diff |= (unsigned int)(unsigned char)cfg[i] ^ (unsigned int)(unsigned char)got[i];
    }
    if (diff != 0U) {
        respond_error(c, 1001, "网关鉴权失败");
        return 0;
    }
    return 1;
}
