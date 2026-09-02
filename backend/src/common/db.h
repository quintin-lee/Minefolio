#pragma once
#include "csilk/drivers/db.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** @brief Initialize database pool from environment/config. Returns 0 on success. */
int db_init(csilk_db_pool_t** out_pool);

/** @brief Run the migration SQL file against the pool. Returns 0 on success. */
int db_run_migrations(csilk_db_pool_t* pool);

/** @brief Get the global database pool singleton. */
csilk_db_pool_t* db_get_pool(void);

/** @brief Returns 1 if the active driver is postgres, 0 for sqlite. */
int db_is_postgres(void);

/**
 * @brief Read a numeric field from a DB query-result row.
 *
 * csilk_db_query_json() returns every column as a JSON *string* node (no type
 * inference), so csilk_json_get_number() would yield 0 for columns holding
 * numbers. These helpers transparently parse string, number, and boolean nodes.
 */
static inline double
db_get_num(const csilk_json_t* obj, const char* key)
{
    const csilk_json_t* v = csilk_json_get(obj, key);
    if (!v) {
        return 0.0;
    }
    if (csilk_json_is_number(v)) {
        return csilk_json_number_value(v);
    }
    if (csilk_json_is_bool(v)) {
        return csilk_json_bool_value(v) ? 1.0 : 0.0;
    }
    if (csilk_json_is_string(v)) {
        const char* s = csilk_json_string_value(v);
        if (!s) {
            return 0.0;
        }
        if (strcasecmp(s, "true") == 0 || strcasecmp(s, "t") == 0) {
            return 1.0;
        }
        if (strcasecmp(s, "false") == 0 || strcasecmp(s, "f") == 0) {
            return 0.0;
        }
        return atof(s);
    }
    return 0.0;
}

/** @brief Integer variant of db_get_num(). */
static inline int64_t
db_get_int(const csilk_json_t* obj, const char* key)
{
    return (int64_t)db_get_num(obj, key);
}

/** @brief Boolean helper supporting json bool, string, and number representations. */
static inline int
db_get_bool(const csilk_json_t* obj, const char* key)
{
    const csilk_json_t* v = csilk_json_get(obj, key);
    if (!v) {
        return 0;
    }
    if (csilk_json_is_bool(v)) {
        return csilk_json_bool_value(v) ? 1 : 0;
    }
    if (csilk_json_is_number(v)) {
        return csilk_json_number_value(v) != 0.0 ? 1 : 0;
    }
    if (csilk_json_is_string(v)) {
        const char* s = csilk_json_string_value(v);
        if (!s) {
            return 0;
        }
        if (strcasecmp(s, "true") == 0 || strcasecmp(s, "t") == 0 || strcasecmp(s, "1") == 0) {
            return 1;
        }
        if (strcasecmp(s, "false") == 0 || strcasecmp(s, "f") == 0 || strcasecmp(s, "0") == 0) {
            return 0;
        }
        return atof(s) != 0.0 ? 1 : 0;
    }
    return 0;
}
