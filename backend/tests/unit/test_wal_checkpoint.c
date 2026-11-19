#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "common/db.h"
#include "csilk/csilk.h"
#include "csilk/drivers/db.h"

int
main(void)
{
    printf("=== Starting WAL Checkpoint Unit Tests ===\n");
    csilk_db_init();

    const char* test_db_path = "./data/test_wal_chkpt.db";
    const char* test_wal_path = "./data/test_wal_chkpt.db-wal";

    /* Ensure data directory exists */
    mkdir("data", 0755);

    /* Clean up pre-existing test files */
    unlink(test_db_path);
    unlink(test_wal_path);

    /* 1. Open SQLite database in WAL mode */
    csilk_db_pool_t* pool = csilk_db_pool_new("sqlite", test_db_path);
    assert(pool != NULL);

    csilk_db_exec(pool, "PRAGMA journal_mode=WAL;");
    csilk_db_exec(pool, "CREATE TABLE test_data (id INTEGER PRIMARY KEY, val TEXT);");

    /* 2. Write data to populate WAL */
    for (int i = 0; i < 50; i++) {
        char sql[128];
        snprintf(sql, sizeof(sql), "INSERT INTO test_data (val) VALUES ('item_%d');", i);
        assert(csilk_db_exec(pool, sql) == 0);
    }

    /* 3. Execute WAL truncate checkpoint */
    int rc = db_wal_checkpoint(pool);
    assert(rc == 0);

    /* 4. Verify WAL file is truncated to 0 bytes if present */
    struct stat st;
    if (stat(test_wal_path, &st) == 0) {
        printf("WAL size after checkpoint: %ld bytes\n", (long)st.st_size);
        assert(st.st_size == 0);
    }

    /* Clean up */
    csilk_db_pool_free(pool);
    unlink(test_db_path);
    unlink(test_wal_path);

    printf("=== WAL Checkpoint Unit Tests Passed! ===\n");
    return 0;
}
