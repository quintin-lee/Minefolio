#include "infrastructure/database/transaction.h"
#include "infrastructure/database/adapter_ops.h"
#include "infrastructure/database/statement.h"
#include <stdlib.h>

extern void*                      mf_db_get_native_handle(mf_db_t* db);
extern const mf_db_adapter_ops_t* mf_db_get_ops(mf_db_t* db);

struct mf_tx_s {
    mf_db_t*                   db;
    void*                      native_handle;
    const mf_db_adapter_ops_t* ops;
    bool                       active;
};

int
mf_tx_begin(mf_db_t* db, mf_tx_t** out_tx)
{
    if (!db || !out_tx) {
        return -1;
    }
    const mf_db_adapter_ops_t* ops = mf_db_get_ops(db);
    void*                      h = mf_db_get_native_handle(db);
    if (!ops || !h) {
        return -1;
    }

    if (ops->tx_begin(h) != 0) {
        return -1;
    }

    mf_tx_t* tx = calloc(1, sizeof(*tx));
    if (!tx) {
        ops->tx_rollback(h);
        return -1;
    }
    tx->db = db;
    tx->native_handle = h;
    tx->ops = ops;
    tx->active = true;

    *out_tx = tx;
    return 0;
}

int
mf_tx_commit(mf_tx_t* tx)
{
    if (!tx || !tx->active) {
        return -1;
    }
    int rc = tx->ops->tx_commit(tx->native_handle);
    tx->active = false;
    free(tx);
    return rc;
}

int
mf_tx_rollback(mf_tx_t* tx)
{
    if (!tx || !tx->active) {
        return -1;
    }
    int rc = tx->ops->tx_rollback(tx->native_handle);
    tx->active = false;
    free(tx);
    return rc;
}

int
mf_tx_savepoint(mf_tx_t* tx, const char* name)
{
    if (!tx || !tx->active || !name || !name[0]) {
        return -1;
    }
    return tx->ops->tx_savepoint(tx->native_handle, name);
}

int
mf_tx_rollback_to_savepoint(mf_tx_t* tx, const char* name)
{
    if (!tx || !tx->active || !name || !name[0]) {
        return -1;
    }
    return tx->ops->tx_rollback_to_savepoint(tx->native_handle, name);
}

int
mf_tx_release_savepoint(mf_tx_t* tx, const char* name)
{
    if (!tx || !tx->active || !name || !name[0]) {
        return -1;
    }
    return tx->ops->tx_release_savepoint(tx->native_handle, name);
}

int
mf_tx_execute(mf_tx_t* tx, const char* sql)
{
    if (!tx || !tx->active || !sql) {
        return -1;
    }
    return tx->ops->execute(tx->native_handle, sql);
}

int
mf_tx_prepare(mf_tx_t* tx, const char* sql, mf_stmt_t** out_stmt)
{
    if (!tx || !tx->active || !sql || !out_stmt) {
        return -1;
    }
    return mf_stmt_prepare(tx->db, sql, out_stmt);
}
