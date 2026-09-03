/**
 * @file transaction_write.c
 * @brief 交易写入服务兼容门面 (Service Facade)
 * @note 平滑过渡门面：将写请求委托至 interfaces/http/controllers/transaction_controller 与 application 用例
 */

#include "services/transaction_service.h"
#include "interfaces/http/controllers/transaction_controller.h"
#include "common/ctx.h"

void
transactions_create(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    api_transactions_create(c);
}

void
transactions_update(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    api_transactions_update(c);
}

void
transactions_delete(csilk_ctx_t* c)
{
    int64_t user_id = ctx_user_id(c);
    if (user_id < 0) {
        return;
    }

    int64_t ledger_id = ctx_ledger_id(c, user_id, "editor");
    if (ledger_id < 0) {
        return;
    }

    api_transactions_delete(c);
}
