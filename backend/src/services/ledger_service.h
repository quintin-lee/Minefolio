#pragma once
#include "csilk/csilk.h"

void ledger_service_list(csilk_ctx_t* c);
void ledger_service_create(csilk_ctx_t* c);
void ledger_service_get(csilk_ctx_t* c);
void ledger_service_update(csilk_ctx_t* c);
void ledger_service_delete(csilk_ctx_t* c);

void ledger_service_list_members(csilk_ctx_t* c);
void ledger_service_add_member(csilk_ctx_t* c);
void ledger_service_update_member(csilk_ctx_t* c);
void ledger_service_remove_member(csilk_ctx_t* c);

void ledger_service_create_invite_code(csilk_ctx_t* c);
void ledger_service_join_by_invite(csilk_ctx_t* c);
