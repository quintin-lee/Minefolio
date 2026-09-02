/**
 * @file receipt_controller.c
 * @brief 购物小票/发票凭证 OCR 智能识别控制器实现文件
 *
 * 遵循三层 C 架构规范，本控制器定义 HTTP 路由分发入口，调用
 * services/receipt_service.c 中的图片处理与多模态大模型结构化提取逻辑。
 */

#include "controllers/receipt_controller.h"
#include "services/receipt_service.h"

/**
 * @brief 小票/凭证识别 HTTP 路由处理入口
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void
receipt_scan_handler(csilk_ctx_t* c)
{
    receipt_service_scan(c);
}

/**
 * @brief 注册票据 OCR 扫描相关的 HTTP 路由
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
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
