#include "services/report_holdings_service.h"
#include "interfaces/http/controllers/portfolio_controller.h"

void
summary_get(csilk_ctx_t* c)
{
    api_portfolio_dashboard_summary(c);
}

void
report_holdings(csilk_ctx_t* c)
{
    api_portfolio_holdings(c);
}
