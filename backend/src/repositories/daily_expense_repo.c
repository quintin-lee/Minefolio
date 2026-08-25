#include "repositories/daily_expense_repo.h"
#include "common/db.h"
#include <stdio.h>
#include <string.h>

csilk_json_t* de_list(csilk_db_pool_t* pool, int64_t user_id, int64_t page, int64_t page_size,
                       const char* expense_type, const char* category_id, const char* tag_ids,
                       const char* start_date, const char* end_date, int64_t* total) {
    char uid[32], limit[32], offset[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(limit, sizeof(limit), "%lld", (long long)page_size);
    snprintf(offset, sizeof(offset), "%lld", (long long)((page - 1) * page_size));
    char sql[2048], count_sql[1024];
    const char* params[16], *cnt_params[16];
    int pidx = 0, cnt_pidx = 0;
    params[pidx++] = uid; cnt_params[cnt_pidx++] = uid;
    snprintf(sql, sizeof(sql), "SELECT de.id,de.user_id,de.category_id,de.asset_id,de.expense_type,de.amount,de.currency,de.expense_date,de.note,de.created_at,de.updated_at,c.name as category_name,a.name as asset_name,(SELECT json_group_array(json_object('id',t.id,'name',t.name,'color',t.color)) FROM expense_tags et JOIN tags t ON et.tag_id=t.id WHERE et.expense_id=de.id) as tags FROM daily_expenses de LEFT JOIN categories c ON de.category_id=c.id LEFT JOIN assets a ON de.asset_id=a.id WHERE de.user_id=?");
    snprintf(count_sql, sizeof(count_sql), "SELECT COUNT(*) AS cnt FROM daily_expenses de WHERE de.user_id=?");
    if (expense_type && expense_type[0]) {
        strncat(sql, " AND de.expense_type=?", sizeof(sql)-strlen(sql)-1);
        strncat(count_sql, " AND de.expense_type=?", sizeof(count_sql)-strlen(count_sql)-1);
        params[pidx++] = expense_type; cnt_params[cnt_pidx++] = expense_type;
    }
    if (category_id && category_id[0]) {
        strncat(sql, " AND de.category_id=?", sizeof(sql)-strlen(sql)-1);
        strncat(count_sql, " AND de.category_id=?", sizeof(count_sql)-strlen(count_sql)-1);
        params[pidx++] = category_id; cnt_params[cnt_pidx++] = category_id;
    }
    if (tag_ids && tag_ids[0]) {
        char tag_bufs[32][32]; const char* tag_ptrs[32]; int tag_count = 0;
        size_t pos = 0;
        while (tag_ids[pos]) {
            while (tag_ids[pos] == ',' || tag_ids[pos] == ' ') pos++;
            if (!tag_ids[pos]) break;
            size_t start = pos;
            while (tag_ids[pos] && tag_ids[pos] != ',' && tag_ids[pos] != ' ') pos++;
            size_t len = pos - start;
            if (len == 0 || len >= sizeof(tag_bufs[0]) || tag_count >= 32) break;
            memcpy(tag_bufs[tag_count], tag_ids + start, len);
            tag_bufs[tag_count][len] = '\0';
            int ti = tag_count++;
            tag_ptrs[ti] = tag_bufs[ti];
        }
        char in_clause[512] = {0}; int ipos = 0;
        for (int i = 0; i < tag_count; i++) { if (i) in_clause[ipos++] = ','; ipos += snprintf(in_clause+ipos, sizeof(in_clause)-(size_t)ipos, " ?"); }
        char filter[512];
        snprintf(filter, sizeof(filter), " AND EXISTS (SELECT 1 FROM expense_tags et2 WHERE et2.expense_id=de.id AND et2.tag_id IN (%s))", in_clause);
        strncat(sql, filter, sizeof(sql)-strlen(sql)-1);
        strncat(count_sql, filter, sizeof(count_sql)-strlen(count_sql)-1);
        for (int i = 0; i < tag_count && pidx < 15; i++) params[pidx++] = tag_ptrs[i];
    }
    if (start_date && start_date[0]) {
        strncat(sql, " AND de.expense_date >= ?", sizeof(sql)-strlen(sql)-1);
        strncat(count_sql, " AND de.expense_date >= ?", sizeof(count_sql)-strlen(count_sql)-1);
        params[pidx++] = start_date; cnt_params[cnt_pidx++] = start_date;
    }
    if (end_date && end_date[0]) {
        strncat(sql, " AND de.expense_date <= ?", sizeof(sql)-strlen(sql)-1);
        strncat(count_sql, " AND de.expense_date <= ?", sizeof(count_sql)-strlen(count_sql)-1);
        params[pidx++] = end_date; cnt_params[cnt_pidx++] = end_date;
    }
    strncat(sql, " ORDER BY de.expense_date DESC LIMIT ? OFFSET ?", sizeof(sql)-strlen(sql)-1);
    params[pidx++] = limit; params[pidx++] = offset; params[pidx] = NULL;
    cnt_params[cnt_pidx] = NULL;
    csilk_json_t* cnt_res = csilk_db_query_param_json(pool, count_sql, cnt_params);
    *total = 0;
    if (cnt_res && csilk_json_array_size(cnt_res) > 0) *total = db_get_int(csilk_json_array_get(cnt_res, 0), "cnt");
    if (cnt_res) csilk_json_free(cnt_res);
    return csilk_db_query_param_json(pool, sql, params);
}
csilk_json_t* de_monthly_totals(csilk_db_pool_t* pool, int64_t user_id, const char* pattern) {
    (void)user_id;
    return csilk_db_query_param_json(pool, "SELECT COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as total_income,COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as total_expense FROM daily_expenses WHERE expense_date LIKE ?", (const char*[]){ pattern, NULL });
}
csilk_json_t* de_monthly_by_category(csilk_db_pool_t* pool, int64_t user_id, const char* pattern) {
    (void)user_id;
    return csilk_db_query_param_json(pool, "SELECT c.name as category_name,de.expense_type,SUM(de.amount) as amount FROM daily_expenses de JOIN categories c ON de.category_id=c.id WHERE de.expense_date LIKE ? GROUP BY c.name,de.expense_type ORDER BY amount DESC", (const char*[]){ pattern, NULL });
}
csilk_json_t* de_monthly_by_tag(csilk_db_pool_t* pool, int64_t user_id, const char* pattern) {
    (void)user_id;
    return csilk_db_query_param_json(pool, "SELECT t.name as tag_name,SUM(de.amount) as amount,COUNT(*) as count FROM daily_expenses de JOIN expense_tags et ON de.id=et.expense_id JOIN tags t ON et.tag_id=t.id WHERE de.expense_date LIKE ? GROUP BY t.name ORDER BY amount DESC", (const char*[]){ pattern, NULL });
}
csilk_json_t* de_monthly_daily(csilk_db_pool_t* pool, int64_t user_id, const char* pattern) {
    (void)user_id;
    return csilk_db_query_param_json(pool, "SELECT expense_date,COALESCE(SUM(CASE WHEN expense_type='income' THEN amount ELSE 0 END),0) as income,COALESCE(SUM(CASE WHEN expense_type='expense' THEN amount ELSE 0 END),0) as expense FROM daily_expenses WHERE expense_date LIKE ? GROUP BY expense_date ORDER BY expense_date", (const char*[]){ pattern, NULL });
}
int64_t de_insert(csilk_db_pool_t* pool, int64_t user_id, int64_t category_id, int64_t asset_id, const char* expense_type, double amount, const char* currency, const char* date, const char* note) {
    char uid[32], cat[32], ast[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    csilk_json_t* res = csilk_db_query_param_json(pool, "INSERT INTO daily_expenses (user_id,category_id,asset_id,expense_type,amount,currency,expense_date,note) VALUES (?,?,?,?,?,?,?,?) RETURNING id", (const char*[]){ uid, cat, ast, expense_type, amt, currency?currency:"CNY", date, note?note:"", NULL });
    int64_t id = 0;
    if (res && csilk_json_array_size(res) > 0) id = db_get_int(csilk_json_array_get(res, 0), "id");
    if (res) csilk_json_free(res);
    return id;
}
csilk_json_t* de_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    return csilk_db_query_param_json(pool, "SELECT amount,expense_type,asset_id FROM daily_expenses WHERE id=? AND user_id=?", (const char*[]){ idstr, uid, NULL });
}
int de_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int64_t category_id, int64_t asset_id, const char* expense_type, double amount, const char* currency, const char* date, const char* note) {
    char uid[32], idstr[32], cat[32], ast[32], amt[64];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    snprintf(cat, sizeof(cat), "%lld", (long long)category_id);
    snprintf(ast, sizeof(ast), "%lld", (long long)asset_id);
    snprintf(amt, sizeof(amt), "%.6f", amount);
    csilk_json_t* res = csilk_db_query_param_json(pool, "UPDATE daily_expenses SET category_id=?,asset_id=?,expense_type=?,amount=?,currency=?,expense_date=?,note=?,updated_at=CURRENT_TIMESTAMP WHERE id=? AND user_id=?", (const char*[]){ cat, ast, expense_type?expense_type:"", amt, currency?currency:"CNY", date?date:"", note?note:"", idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
int de_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id) {
    char uid[32], idstr[32];
    snprintf(uid, sizeof(uid), "%lld", (long long)user_id);
    snprintf(idstr, sizeof(idstr), "%lld", (long long)id);
    csilk_json_t* res = csilk_db_query_param_json(pool, "DELETE FROM daily_expenses WHERE id=? AND user_id=?", (const char*[]){ idstr, uid, NULL });
    int ok = res ? csilk_json_array_size(res) > 0 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
int de_tag_insert(csilk_db_pool_t* pool, int64_t expense_id, int64_t tag_id) {
    char eid[32], tid[32];
    snprintf(eid, sizeof(eid), "%lld", (long long)expense_id);
    snprintf(tid, sizeof(tid), "%lld", (long long)tag_id);
    csilk_json_t* res = csilk_db_query_param_json(pool, "INSERT OR IGNORE INTO expense_tags (expense_id,tag_id) VALUES (?,?)", (const char*[]){ eid, tid, NULL });
    int ok = res ? 1 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
int de_tag_delete_all(csilk_db_pool_t* pool, int64_t expense_id) {
    char eid[32];
    snprintf(eid, sizeof(eid), "%lld", (long long)expense_id);
    csilk_json_t* res = csilk_db_query_param_json(pool, "DELETE FROM expense_tags WHERE expense_id=?", (const char*[]){ eid, NULL });
    int ok = res ? 1 : 0;
    if (res) csilk_json_free(res);
    return ok;
}
