#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <sys/stat.h>

#include "infrastructure/database/db_backup.h"
#include "common/db.h"
#include "csilk/csilk.h"
#include "csilk/drivers/db.h"

int
main(void)
{
    printf("=== Starting Database Hot Backup Unit Tests ===\n");
    csilk_db_init();

    /* 1. Test RF-01: Path Traversal defense in filename validation */
    assert(db_backup_is_valid_filename("../../etc/passwd") == false);
    assert(db_backup_is_valid_filename("../secret.db") == false);
    assert(db_backup_is_valid_filename("sub/folder.db") == false);
    assert(db_backup_is_valid_filename("sub\\folder.db") == false);
    assert(db_backup_is_valid_filename("test") == false);
    assert(db_backup_is_valid_filename(".db") == false);
    assert(db_backup_is_valid_filename("bad!name.db") == false);
    assert(db_backup_is_valid_filename("minefolio_backup_20261119_120000.db") == true);
    assert(db_backup_is_valid_filename("backup-test_1.2.db") == true);
    printf("Test 1: Path traversal validation passed\n");

    const char* test_db_path = "./data/test_backup_src.db";
    const char* test_backup_dir = "./data/test_backups_dir";

    /* Cleanup test artifacts */
    unlink(test_db_path);
    char cleanup_cmd[256];
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf %s", test_backup_dir);
    system(cleanup_cmd);

    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", test_db_path);
    assert(pool != NULL);

    csilk_db_exec(pool, "PRAGMA journal_mode=WAL;");
    csilk_db_exec(pool, "CREATE TABLE accounts (id INTEGER PRIMARY KEY, name TEXT, balance REAL);");
    csilk_db_exec(pool, "INSERT INTO accounts (name, balance) VALUES ('Cash', 5000.0);");
    csilk_db_exec(pool, "INSERT INTO accounts (name, balance) VALUES ('Bank', 25000.0);");

    /* 2. Test RF-03: Create backup in non-existent directory */
    char backup_fn[128];
    memset(backup_fn, 0, sizeof(backup_fn));
    int rc = db_backup_create(pool, test_backup_dir, backup_fn, sizeof(backup_fn));
    assert(rc == 0);
    assert(strlen(backup_fn) > 0);
    assert(strstr(backup_fn, ".db") != NULL);
    printf("Test 2: Backup created successfully: %s\n", backup_fn);

    /* 3. Test RF-02: Integrity check on generated backup database */
    char full_backup_path[256];
    snprintf(full_backup_path, sizeof(full_backup_path), "%s/%s", test_backup_dir, backup_fn);

    sqlite3* b_db = NULL;
    assert(sqlite3_open_v2(full_backup_path, &b_db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK);
    sqlite3_stmt* stmt = NULL;
    assert(sqlite3_prepare_v2(b_db, "PRAGMA integrity_check;", -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    const unsigned char* check_res = sqlite3_column_text(stmt, 0);
    assert(check_res != NULL && strcmp((const char*)check_res, "ok") == 0);
    sqlite3_finalize(stmt);
    sqlite3_close(b_db);
    printf("Test 3: Integrity check passed on backup database\n");

    /* 4. Test db_backup_get_status */
    csilk_json_t* status = NULL;
    assert(db_backup_get_status(pool, test_backup_dir, &status) == 0);
    assert(status != NULL);
    assert(strcmp(csilk_json_get_string(status, "db_driver"), "sqlite") == 0);
    assert(csilk_json_get_int(status, "backup_count") == 1);
    assert(csilk_json_get_int(status, "db_size") > 0);
    assert(csilk_json_get_int(status, "disk_free_bytes") > 0);
    csilk_json_t* arr = csilk_json_get(status, "backups");
    assert(arr != NULL && csilk_json_array_size(arr) == 1);
    csilk_json_free(status);
    printf("Test 4: Backup status reported accurately\n");

    /* 5. Test db_backup_rotate */
    /* Create 3 more dummy backup files */
    for (int i = 1; i <= 3; i++) {
        char dummy_path[256];
        snprintf(dummy_path, sizeof(dummy_path), "%s/minefolio_backup_dummy_%d.db", test_backup_dir, i);
        FILE* fp = fopen(dummy_path, "wb");
        assert(fp != NULL);
        fputs("dummy_sqlite_header", fp);
        fclose(fp);
        usleep(15000); /* slight mtime diff */
    }

    assert(db_backup_rotate(test_backup_dir, 2) == 0);

    /* Verify only 2 files remain */
    status = NULL;
    assert(db_backup_get_status(pool, test_backup_dir, &status) == 0);
    assert(csilk_json_get_int(status, "backup_count") == 2);
    csilk_json_free(status);
    printf("Test 5: Backup rotation successfully retained 2 latest files\n");

    /* 6. Test db_backup_delete */
    assert(db_backup_delete(test_backup_dir, "../../etc/passwd") != 0);
    assert(db_backup_delete(test_backup_dir, "minefolio_backup_dummy_3.db") == 0);

    status = NULL;
    assert(db_backup_get_status(pool, test_backup_dir, &status) == 0);
    assert(csilk_json_get_int(status, "backup_count") == 1);
    csilk_json_free(status);
    printf("Test 6: Safe delete and traversal protection verified\n");

    /* Clean up */
    csilk_db_pool_free(pool);
    unlink(test_db_path);
    system(cleanup_cmd);

    printf("=== Database Hot Backup Unit Tests Passed! ===\n");
    return 0;
}
