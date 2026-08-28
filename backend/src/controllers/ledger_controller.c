#include "controllers/ledger_controller.h"
#include "services/ledger_service.h"

void
register_ledger_routes(csilk_app_t* app)
{
    csilk_app_get_ext(app,
                      "/api/ledgers",
                      ledger_service_list,
                      NULL,
                      NULL,
                      "List ledgers",
                      "Get all ledgers current user owns or belongs to");
    csilk_app_post_ext(app,
                       "/api/ledgers",
                       ledger_service_create,
                       NULL,
                       NULL,
                       "Create ledger",
                       "Create a new ledger");
    csilk_app_get_ext(app,
                      "/api/ledgers/:id",
                      ledger_service_get,
                      NULL,
                      NULL,
                      "Get ledger",
                      "Get ledger details");
    csilk_app_put_ext(app,
                      "/api/ledgers/:id",
                      ledger_service_update,
                      NULL,
                      NULL,
                      "Update ledger",
                      "Update ledger metadata");
    csilk_app_delete_ext(app,
                         "/api/ledgers/:id",
                         ledger_service_delete,
                         NULL,
                         NULL,
                         "Delete ledger",
                         "Delete ledger and associated items");

    csilk_app_get_ext(app,
                      "/api/ledgers/:id/members",
                      ledger_service_list_members,
                      NULL,
                      NULL,
                      "List members",
                      "Get ledger members and roles");
    csilk_app_post_ext(app,
                       "/api/ledgers/:id/members",
                       ledger_service_add_member,
                       NULL,
                       NULL,
                       "Add member",
                       "Add member by username");
    csilk_app_put_ext(app,
                      "/api/ledgers/:id/members/:user_id",
                      ledger_service_update_member,
                      NULL,
                      NULL,
                      "Update member role",
                      "Update member role");
    csilk_app_delete_ext(app,
                         "/api/ledgers/:id/members/:user_id",
                         ledger_service_remove_member,
                         NULL,
                         NULL,
                         "Remove member",
                         "Remove member or leave ledger");

    csilk_app_post_ext(app,
                       "/api/ledgers/:id/invite-code",
                       ledger_service_create_invite_code,
                       NULL,
                       NULL,
                       "Generate invite code",
                       "Generate or refresh 6-digit invite code");
    csilk_app_post_ext(app,
                       "/api/ledgers/join",
                       ledger_service_join_by_invite,
                       NULL,
                       NULL,
                       "Join by invite code",
                       "Join a ledger using invite code");
}
