/**
 * @file file_controller.h
 * @brief 文件上传与多格式文档解析控制器头文件
 *
 * 声明通用文件上传、流式 Multipart 数据接收以及
 * 多格式文本抽取（PDF / TXT / CSV / ZIP）相关的 HTTP 路由注册与处理函数。
 */

#pragma once
#include "csilk/csilk.h"

/**
 * @brief 处理文件上传与文本提取解析请求
 *
 * @details HTTP 方法: POST
 *          REST 路径: /api/files/upload
 *          鉴权要求: JWT 认证 (Bearer Token)
 *          请求体:
 *          - Multipart/form-data 格式上传二进制或文本文件（支持 PDF, TXT, CSV, ZIP）
 *          返回包格式:
 *          - 200 OK: {"code": 0, "message": "ok", "data": {"filename": "statement.pdf", "size": 1048576, "content": "...", "status": "parsed"}}
 *          - 400 Bad Request: 未上传有效文件 (code: 1002)
 *          - 401 Unauthorized: 未登录 (code: 1001)
 *
 *          通过流式回调接收上传分片，内存组装后调用 services/file_parser.h 解析文档内容返回纯文本。
 *
 * @param[in,out] c HTTP 请求上下文指针 (csilk_ctx_t*)
 */
void file_upload_handler(csilk_ctx_t* c);

#include "csilk/app/app.h"

/**
 * @brief 注册文件上传与解析相关的 HTTP 路由
 *
 * @details 将 POST /api/files/upload 端点挂载至 Csilk 应用实例。
 *
 * @param[in,out] app Csilk 应用实例指针 (csilk_app_t*)
 */
void register_file_routes(csilk_app_t* app);
