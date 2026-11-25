#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/db.h"
#include "application/auth/commands.h"
#include "application/auth/dtos.h"
#include "application/auth/usecases.h"
#include "domain/auth/repository.h"

int
main(void)
{
    printf("=== Running test_admin_user_management ===\n");
    unlink("/tmp/test_admin_user_management.db");
    setenv("MINEFOLIO_DB_DSN", "/tmp/test_admin_user_management.db", 1);
    setenv("DB_DRIVER", "sqlite", 1);
    setenv("JWT_SECRET", "test_secret_for_admin_user_mgmt_123456", 1);

    csilk_db_pool_t* pool = NULL;
    assert(db_init(&pool) == 0 && pool != NULL);
    assert(db_run_migrations(pool) == 0);

    /* 1. 首个注册用户自动成为管理员 */
    register_user_cmd_t first_reg = {
        .username = "root_admin",
        .plain_password = "AdminPass123!",
    };
    csilk_json_t*         reg_data = NULL;
    auth_usecase_result_t res = {0};
    assert(auth_usecase_register(pool, &first_reg, NULL, &reg_data, &res) == 0);
    assert(reg_data != NULL);
    csilk_json_free(reg_data);
    reg_data = NULL;

    mf_user_t admin_user = {0};
    assert(mf_auth_repo_find_by_username(pool, "root_admin", &admin_user) == 0);
    assert(strcmp(admin_user.role, "admin") == 0);
    assert(admin_user.disabled == false);
    assert(auth_usecase_is_instance_admin(pool, admin_user.id) == 1);
    printf("PASS: first registered user is admin\n");

    /* 2. 管理员创建普通用户与第二个管理员 */
    admin_create_user_cmd_t create_bob = {
        .username = "bob_user",
        .plain_password = "BobPass123!",
        .role = "user",
    };
    csilk_json_t* bob_json = NULL;
    assert(auth_usecase_admin_create_user(pool, &create_bob, NULL, &bob_json, &res) == 0);
    assert(bob_json != NULL);
    int64_t bob_id = (int64_t)csilk_json_get_number(bob_json, "id");
    assert(bob_id > 0);
    assert(strcmp(csilk_json_get_string(bob_json, "role"), "user") == 0);
    assert(auth_usecase_is_instance_admin(pool, bob_id) == 0);
    csilk_json_free(bob_json);

    admin_create_user_cmd_t create_alice = {
        .username = "alice_admin",
        .plain_password = "AlicePass123!",
        .role = "admin",
    };
    csilk_json_t* alice_json = NULL;
    assert(auth_usecase_admin_create_user(pool, &create_alice, NULL, &alice_json, &res) == 0);
    assert(alice_json != NULL);
    int64_t alice_id = (int64_t)csilk_json_get_number(alice_json, "id");
    assert(alice_id > 0);
    assert(auth_usecase_is_instance_admin(pool, alice_id) == 1);
    csilk_json_free(alice_json);

    /* 重复用户名拒绝 */
    csilk_json_t* dup_json = NULL;
    assert(auth_usecase_admin_create_user(pool, &create_bob, NULL, &dup_json, &res) != 0);
    assert(res.code == 1004);
    printf("PASS: admin create user & duplicate username check\n");

    /* 3. 分页查询、关键词与角色筛选、统计 */
    admin_list_users_cmd_t list_all = {.page = 1, .page_size = 10};
    csilk_json_t*          list_resp = NULL;
    assert(auth_usecase_admin_list_users(pool, &list_all, &list_resp, &res) == 0);
    assert((int64_t)csilk_json_get_number(list_resp, "total") == 3);
    csilk_json_t* stats = csilk_json_get(list_resp, "stats");
    assert((int64_t)csilk_json_get_number(stats, "total_users") == 3);
    assert((int64_t)csilk_json_get_number(stats, "admin_users") == 2);
    assert((int64_t)csilk_json_get_number(stats, "active_admin_users") == 2);
    csilk_json_free(list_resp);

    admin_list_users_cmd_t list_kw = {.page = 1, .page_size = 10, .keyword = "bob", .role = "user"};
    assert(auth_usecase_admin_list_users(pool, &list_kw, &list_resp, &res) == 0);
    assert((int64_t)csilk_json_get_number(list_resp, "total") == 1);
    csilk_json_free(list_resp);
    printf("PASS: admin list users with filters and stats\n");

    /* 4. 禁用用户并验证登录与 /me 均被拒绝 */
    admin_update_user_cmd_t disable_bob = {
        .actor_user_id = admin_user.id,
        .target_user_id = bob_id,
        .has_disabled = true,
        .disabled = true,
    };
    csilk_json_t* upd_json = NULL;
    assert(auth_usecase_admin_update_user(pool, &disable_bob, &upd_json, &res) == 0);
    assert(csilk_json_as_bool(csilk_json_get(upd_json, "disabled"), false) == true);
    csilk_json_free(upd_json);

    login_user_cmd_t bob_login = {.username = "bob_user", .plain_password = "BobPass123!"};
    csilk_json_t*    login_out = NULL;
    assert(auth_usecase_login(pool, &bob_login, NULL, &login_out, &res) != 0);
    assert(res.code == 1004);

    csilk_json_t* me_out = NULL;
    assert(auth_usecase_me(pool, bob_id, &me_out, &res) != 0);
    assert(res.code == 1001);

    /* 重新启用 Bob */
    admin_update_user_cmd_t enable_bob = {
        .actor_user_id = admin_user.id,
        .target_user_id = bob_id,
        .has_disabled = true,
        .disabled = false,
    };
    assert(auth_usecase_admin_update_user(pool, &enable_bob, NULL, &res) == 0);
    assert(auth_usecase_login(pool, &bob_login, NULL, &login_out, &res) == 0);
    csilk_json_free(login_out);
    printf("PASS: disable/enable user blocks & restores login\n");

    /* 5. 重置密码并验证旧密码失效、新密码可登录 */
    admin_reset_password_cmd_t reset_bob = {
        .target_user_id = bob_id,
        .plain_password = "NewBobPass456!",
    };
    assert(auth_usecase_admin_reset_password(pool, &reset_bob, NULL, &res) == 0);
    assert(auth_usecase_login(pool, &bob_login, NULL, &login_out, &res) != 0);
    bob_login.plain_password = "NewBobPass456!";
    assert(auth_usecase_login(pool, &bob_login, NULL, &login_out, &res) == 0);
    csilk_json_free(login_out);
    printf("PASS: admin reset password updates credentials\n");

    /* 6. 管理员关闭指定用户的 2FA */
    mf_auth_repo_set_totp_secret(pool, bob_id, "JBSWY3DPEHPK3PXP");
    mf_auth_repo_enable_totp(pool, bob_id, "[\"CODE0001\"]");
    assert(auth_usecase_admin_disable_2fa(pool, bob_id, &res) == 0);
    mf_user_t bob_after = {0};
    assert(mf_auth_repo_get_by_id(pool, bob_id, &bob_after) == 0);
    assert(bob_after.totp_enabled == false);
    printf("PASS: admin disable user 2FA\n");

    /* 7. 自保护与最后管理员保护 */
    admin_update_user_cmd_t self_disable = {
        .actor_user_id = admin_user.id,
        .target_user_id = admin_user.id,
        .has_disabled = true,
        .disabled = true,
    };
    assert(auth_usecase_admin_update_user(pool, &self_disable, NULL, &res) != 0);
    assert(res.code == 1004);
    assert(auth_usecase_admin_delete_user(pool, admin_user.id, admin_user.id, &res) != 0);
    assert(res.code == 1004);

    /* 降级 Alice 后，root_admin 成为唯一可用管理员 */
    admin_update_user_cmd_t demote_alice = {
        .actor_user_id = admin_user.id,
        .target_user_id = alice_id,
        .role = "user",
    };
    assert(auth_usecase_admin_update_user(pool, &demote_alice, NULL, &res) == 0);
    assert(auth_usecase_is_instance_admin(pool, alice_id) == 0);
    assert(mf_auth_repo_count_active_admins(pool) == 1);
    printf("PASS: self-protection and role demotion\n");

    /* 8. 级联删除带默认账本/账户/分类的用户（PRAGMA foreign_keys = ON 下仍成功） */
    assert(auth_usecase_admin_delete_user(pool, admin_user.id, bob_id, &res) == 0);
    assert(mf_auth_repo_get_by_id(pool, bob_id, &bob_after) == 1);
    assert(auth_usecase_user_count(pool) == 2);
    printf("PASS: cascade delete user with seeded ledger/accounts/categories\n");

    unlink("/tmp/test_admin_user_management.db");
    printf("=== All test_admin_user_management tests passed ===\n");
    return 0;
}
