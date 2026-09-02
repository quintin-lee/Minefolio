/**
 * @file receipt_controller.h
 * @brief 购物小票/发票凭证 OCR 智能识别控制器头文件
 *
 * 声明票据凭证图片上传、多模态 AI OCR 结构化文本解析、金额商户自动提取
 * 以及路由注册函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 处理小票/发票图片 OCR 智能扫描请求
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/receipts/scan
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体:
 *          - Multipart/form-data 格式上传小票图片文件 (JPEG / PNG / WebP)
 *            或 JSON 包含 base64 图片数据 {"image": "data:image/jpeg;base64,..."}
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"merchant": "山姆会员商店", "amount": 358.50, "date": "2026-09-02", "category_suggestion": "日常购物", "items": [{"name": "牛肉", "price": 128.0}]}}
 *          - 400 Bad Request: 未上传有效图片或识别失败 (code: 1002)
 *
 *          分发至 services/receipt_service.c 调用底层多模态大模型或 OCR 引擎提取关键记账字段。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void receipt_scan_handler(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册小票凭证 OCR 扫描相关的 HTTP 路由
 *
 * @details 将 POST /api/receipts/scan 端点挂载至 Csilk 应用实例。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_receipt_routes(csilk_app_t* app);
