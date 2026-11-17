#pragma once
#include "csilk/csilk.h"

/**
 * @brief 注册 IM 机器人 (QQ / 微信个人号) 只读查账端点
 *
 *   POST /api/im/bindings/token   App 侧: 签发 300s 受限绑定码
 *   GET  /api/im/bindings         App 侧: 列出我的 IM 绑定
 *   DELETE /api/im/bindings       App 侧: 解绑某平台账号
 *   POST /api/im/:platform/bind   网关回调: 落库绑定
 *   POST /api/im/:platform/chat   网关回调: 只读查账 (非流式)
 */
void register_im_bot_routes(csilk_app_t* app);
