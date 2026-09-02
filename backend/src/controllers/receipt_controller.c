#include "controllers/receipt_controller.h"
#include "services/receipt_service.h"

void
receipt_scan_handler(csilk_ctx_t* c)
{
    receipt_service_scan(c);
}

void
register_receipt_routes(csilk_app_t* app)
{
    csilk_app_post_ext(app,
                       "/api/receipts/scan",
                       receipt_scan_handler,
                       NULL,
                       NULL,
                       "Scan receipt",
                       "Upload receipt image for AI OCR extraction");
}
