#include "services/auth_service.h"
#include "interfaces/http/controllers/auth_controller.h"

void
auth_register(csilk_ctx_t* c)
{
    api_auth_register(c);
}

void
auth_login(csilk_ctx_t* c)
{
    api_auth_login(c);
}

void
auth_change_password(csilk_ctx_t* c)
{
    api_auth_change_password(c);
}

void
auth_me(csilk_ctx_t* c)
{
    api_auth_me(c);
}

void
auth_2fa_status(csilk_ctx_t* c)
{
    api_auth_2fa_status(c);
}

void
auth_2fa_setup(csilk_ctx_t* c)
{
    api_auth_2fa_setup(c);
}

void
auth_2fa_enable(csilk_ctx_t* c)
{
    api_auth_2fa_enable(c);
}

void
auth_2fa_disable(csilk_ctx_t* c)
{
    api_auth_2fa_disable(c);
}

void
auth_2fa_verify_login(csilk_ctx_t* c)
{
    api_auth_2fa_verify_login(c);
}

void
auth_oauth_providers(csilk_ctx_t* c)
{
    api_auth_oauth_providers(c);
}

void
auth_oauth_callback(csilk_ctx_t* c)
{
    api_auth_oauth_callback(c);
}
