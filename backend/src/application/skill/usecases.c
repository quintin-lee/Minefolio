#include "application/skill/usecases.h"
#include "domain/skill/entity.h"
#include "domain/skill/rules.h"
#include "infrastructure/repositories/skill_repo_impl.h"
#include "csilk/drivers/ai/skill.h"
#include "common/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- 4 Default Built-in Financial Domain Skills --- */

static const mf_skill_t S_DEFAULT_SKILLS[] = {
    {.name = "financial-health-auditor",
     .description =
         "全面审计家庭与个人财务健康度：流动性资金覆盖月数、负债收入比、月均储蓄率与风险诊断。",
     .category = "finance",
     .tags = "finance,cashflow,health,debt",
     .license = "MIT",
     .compatibility = "minefolio >= 1.0",
     .enabled = 1,
     .backend = MF_SKILL_BACKEND_LOCAL,
     .is_builtin = 1,
     .instructions_l2 =
         "# 金融健康全面审计专家 (Financial Health Auditor)\n\n"
         "## 角色与职责\n"
         "你是一位严谨的注册理财规划师（CFP），专注于基于用户真实账本流水、资产结构与负债状况进行客"
         "观财务体检。\n\n"
         "## 审计诊断标准\n"
         "1. **流动性应急金（Liquidity Cushion）**：\n"
         "   - 计算：现金+活期银行卡余额 / 最近 3 个月月均总支出。\n"
         "   - 评分：<3 个月为「高危(D)」，3~6 个月为「健康(B)」，6~12 个月为「充足(A)」，>12 "
         "个月提示资金闲置机会成本。\n"
         "2. **负债比率（Debt Service Ratio）**：\n"
         "   - 计算：月度还贷金额（房贷/车贷/信用卡分期） / 月度净收入。\n"
         "   - 安全线：<30% 安全，30%~45% 预警，>45% 处于债务沉重状态。\n"
         "3. **储蓄结余率（Savings Rate）**：\n"
         "   - 计算：(月收入 - 月支出) / 月收入。\n"
         "   - 建议基准：>= 30% 优秀，10%~30% 正常，<10% 或赤字需削减弹性开支。\n\n"
         "## 输出格式\n"
         "以 Markdown 表格输出指标得分，给出 1-2 条当前优先级最高的可落地调整动作。"             },
    {.name = "dca-rebalance-advisor",
     .description = "定投偏离度测算与资产动态再平衡顾问：低吸加仓指引与大类资产配置比例优化。",
     .category = "investment",
     .tags = "investment,dca,rebalance,portfolio",
     .license = "MIT",
     .compatibility = "minefolio >= 1.0",
     .enabled = 1,
     .backend = MF_SKILL_BACKEND_LOCAL,
     .is_builtin = 1,
     .instructions_l2 =
         "# 定投与组合再平衡顾问 (DCA & Rebalance Advisor)\n\n"
         "## 角色与职责\n"
         "你是一位量化资产配置专家，擅长使用现代投资组合理论（MPT）与价值平均法（Value "
         "Averaging）指导用户投资。\n\n"
         "## 再平衡触发法则\n"
         "1. **5/25 偏离规则**：某类资产权重绝对偏离目标比例 5%，或相对偏离 25% "
         "时触发再平衡提醒。\n"
         "2. **低吸优先（Rebalance with "
         "Inflows）**"
         "：尽可能通过定投资金对低配资产加仓完成平衡，避免高卖产生交易损耗与资本利得税。\n"
         "3. **定投金额动态调整**：\n"
         "   - 当标的市盈率/净值处于近 3 年估值历史分位数 < 20% 时：定投基准金额 × 1.3\n"
         "   - 处于 20%~80% 区间：保持 1.0× 基准\n"
         "   - 处于 > 80% 区间：定投基准金额 × 0.7 或停止定投囤积现金等待机会。\n\n"
         "## 输出格式\n"
         "列出各标的当前市值、当前占比、目标占比与建议加仓差额（元）。"                         },
    {.name = "impulse-expense-guard",
     .description =
         "防冲动消费守护者：折算等效工作时长，运用苏格拉底式提问进行消费决策挑战与冷却拦截。",
     .category = "expense",
     .tags = "expense,guard,psychology",
     .license = "MIT",
     .compatibility = "minefolio >= 1.0",
     .enabled = 1,
     .backend = MF_SKILL_BACKEND_LOCAL,
     .is_builtin = 1,
     .instructions_l2 = "# 防冲动消费理财守护者 (Impulse Expense Guard)\n\n"
                        "## 角色与职责\n"
                        "你的任务是帮助用户抵御情绪化冲动消费，建立理性消费护城河。\n\n"
                        "## 消费决策四步法\n"
                        "1. **生命能量换算（Life Energy Equivalent）**：\n"
                        "   - 将待购商品金额，换算为用户需要付出的小时工时（如月薪 15,000 "
                        "元折合时薪 ~85 元，一件 1,200 元衣服需要连续工作 14 小时）。\n"
                        "2. **真需求考验（Need vs Want）**：\n"
                        "   - 该物品在未来 30 天内会使用超过 5 次吗？\n"
                        "   - 家中是否已有具备 80% 同等功能的替代品？\n"
                        "3. **机会成本换算（Opportunity Cost）**：\n"
                        "   - 若将这笔钱投入年化 6% 的稳健资产，3 年后将增值为多少？\n"
                        "4. **48 小时冷却机制**：\n"
                        "   - 超过 500 元的非即时必需品，建议加入愿望清单冷却 48 小时。\n\n"
                        "## 态度与基调\n"
                        "真诚、温和且具说服力，不用说教语气，而是用数据启发用户自省。"          },
    {.name = "receipt-tax-classifier",
     .description =
         "发票与票据税务智能归类专家：增值税合规抵扣、个税专项附加扣除与商用报销类别判定。",
     .category = "tax",
     .tags = "tax,receipt,invoice,classification",
     .license = "MIT",
     .compatibility = "minefolio >= 1.0",
     .enabled = 1,
     .backend = MF_SKILL_BACKEND_LOCAL,
     .is_builtin = 1,
     .instructions_l2 =
         "# 发票票据税务分类专家 (Receipt & Tax Classifier)\n\n"
         "## 角色与职责\n"
         "针对 OCR "
         "扫描或手工录入的消费小票与发票条目，依据财税法规给出精准的账本分类与税务处理建议。\n\n"
         "## 归类指引\n"
         "1. **企业/自由职业商用支出**：\n"
         "   - 差旅交通（高铁/机票/打车）：归入「差旅费」，提取税额。\n"
         "   - 商务宴请/客户招待：归入「业务招待费」，标注限额扣除提示。\n"
         "   - 办公耗材/云服务/软件订阅：归入「办公及信息服务费」，全额税前列支。\n"
         "2. **个人所得税专项扣除对照**：\n"
         "   - 医疗自费票据：累计超过 15,000 元部分提醒可申报大病医疗专项。\n"
         "   - 职业技能证书/培训学费：标记符合继续教育专项附加扣除。\n"
         "3. **家庭账本分类映射**：\n"
         "   - 推荐 Minefolio 标准三级分类体系（如 餐饮食品/日常食材，文化娱乐/电子数码等）。"}
};

static const size_t S_DEFAULT_SKILLS_COUNT = sizeof(S_DEFAULT_SKILLS) / sizeof(S_DEFAULT_SKILLS[0]);

/* --- Use Cases Implementations --- */

int
skill_usecase_list(
    void* pool, int64_t user_id, const char* category, int enabled_only, csilk_json_t** out_data)
{
    if (!pool || !out_data) {
        return -1;
    }
    *out_data = skill_repo_list((csilk_db_pool_t*)pool, user_id, category, enabled_only);
    return (*out_data != NULL) ? 0 : -1;
}

int
skill_usecase_get(void* pool, int64_t user_id, int64_t id, csilk_json_t** out_data)
{
    if (!pool || !out_data) {
        return -1;
    }
    *out_data = skill_repo_get((csilk_db_pool_t*)pool, user_id, id);
    return (*out_data != NULL) ? 0 : 1; // 0 found, 1 not found
}

int
skill_usecase_create(void* pool, int64_t user_id, const mf_skill_create_dto_t* dto, int64_t* out_id)
{
    if (!pool || !dto) {
        return -1;
    }

    mf_skill_t sk = {0};
    snprintf(sk.name, sizeof(sk.name), "%s", dto->name);
    snprintf(sk.description, sizeof(sk.description), "%s", dto->description);
    snprintf(sk.license, sizeof(sk.license), "%s", dto->license);
    snprintf(sk.compatibility, sizeof(sk.compatibility), "%s", dto->compatibility);
    snprintf(sk.category, sizeof(sk.category), "%s", dto->category[0] ? dto->category : "finance");
    snprintf(sk.tags, sizeof(sk.tags), "%s", dto->tags);
    sk.enabled = dto->enabled;
    sk.instructions_l2 = (char*)(dto->instructions_l2 ? dto->instructions_l2 : "");
    sk.backend = dto->backend;
    memcpy(&sk.exec_cfg, &dto->exec_cfg, sizeof(mf_skill_exec_cfg_t));
    sk.is_builtin = dto->is_builtin;

    char err[256] = {0};
    if (mf_skill_validate(&sk, err, sizeof(err)) != 0) {
        return 1002; // validation error
    }

    csilk_json_t* exist = skill_repo_get_by_name((csilk_db_pool_t*)pool, user_id, sk.name);
    if (exist) {
        csilk_json_free(exist);
        return 1004; // duplicate name
    }

    int64_t id = skill_repo_insert((csilk_db_pool_t*)pool, user_id, &sk);
    if (id <= 0) {
        return -1;
    }
    if (out_id) {
        *out_id = id;
    }
    return 0;
}

int
skill_usecase_update(void* pool, int64_t user_id, int64_t id, const mf_skill_update_dto_t* dto)
{
    if (!pool || !dto) {
        return -1;
    }

    csilk_json_t* existing = skill_repo_get((csilk_db_pool_t*)pool, user_id, id);
    if (!existing) {
        return 1002; // not found
    }

    mf_skill_t  sk = {0};
    const char* name = csilk_json_get_string(existing, "name");
    if (name) {
        strncpy(sk.name, name, sizeof(sk.name) - 1);
    }
    csilk_json_free(existing);

    snprintf(sk.description, sizeof(sk.description), "%s", dto->description);
    snprintf(sk.license, sizeof(sk.license), "%s", dto->license);
    snprintf(sk.compatibility, sizeof(sk.compatibility), "%s", dto->compatibility);
    snprintf(sk.category, sizeof(sk.category), "%s", dto->category[0] ? dto->category : "finance");
    snprintf(sk.tags, sizeof(sk.tags), "%s", dto->tags);
    sk.enabled = dto->has_enabled ? (dto->enabled ? 1 : 0) : (int)db_get_int(existing, "enabled");
    sk.instructions_l2 = (char*)(dto->instructions_l2 ? dto->instructions_l2 : "");
    sk.backend = dto->backend;
    memcpy(&sk.exec_cfg, &dto->exec_cfg, sizeof(mf_skill_exec_cfg_t));

    char err[256] = {0};
    if (mf_skill_validate(&sk, err, sizeof(err)) != 0) {
        return 1002;
    }

    return (skill_repo_update((csilk_db_pool_t*)pool, user_id, id, &sk) == 0) ? 0 : -1;
}

int
skill_usecase_delete(void* pool, int64_t user_id, int64_t id)
{
    if (!pool) {
        return -1;
    }
    return (skill_repo_delete((csilk_db_pool_t*)pool, user_id, id) == 0) ? 0 : -1;
}

int
skill_usecase_toggle(void* pool, int64_t user_id, int64_t id, int enabled)
{
    if (!pool) {
        return -1;
    }
    return (skill_repo_toggle((csilk_db_pool_t*)pool, user_id, id, enabled) == 0) ? 0 : -1;
}

int
skill_usecase_test_execute(
    void* pool, int64_t user_id, int64_t id, const char* args, csilk_json_t** out_res)
{
    if (!pool || !out_res) {
        return -1;
    }
    *out_res = NULL;

    csilk_json_t* sk = skill_repo_get((csilk_db_pool_t*)pool, user_id, id);
    if (!sk) {
        return 1002; // not found
    }

    const char* path = csilk_json_get_string(sk, "exec_path");
    const char* root = csilk_json_get_string(sk, "exec_root");
    const char* def_args = csilk_json_get_string(sk, "exec_args");
    const char* def_env = csilk_json_get_string(sk, "exec_env");
    const char* name = csilk_json_get_string(sk, "name");
    const char* desc = csilk_json_get_string(sk, "description");
    const char* l2 = csilk_json_get_string(sk, "instructions_l2");
    int         timeout = db_get_int(sk, "timeout_ms");
    int         max_out = db_get_int(sk, "max_output_bytes");

    csilk_json_t* res = csilk_json_object();

    if (path && path[0] != '\0') {
        // L3 script execution
        csilk_skill_meta_t meta = {0};
        if (name) {
            snprintf(meta.name, sizeof(meta.name), "%s", name);
        }
        /* csilk_ai_skill_new requires non-empty description without '<' or '>' */
        if (desc && desc[0] != '\0') {
            snprintf(meta.description, sizeof(meta.description), "%s", desc);
            for (char* p = meta.description; *p; p++) {
                if (*p == '<') {
                    *p = '(';
                } else if (*p == '>') {
                    *p = ')';
                }
            }
        } else {
            snprintf(meta.description, sizeof(meta.description), "%s", "Minefolio L3 skill");
        }
        csilk_ai_skill_t* csk = csilk_ai_skill_new(&meta, CSILK_SKILL_LOCAL, NULL, NULL);
        if (csk) {
            char* norm_env = NULL;
            if (def_env && def_env[0] != '\0') {
                norm_env = strdup(def_env);
                if (norm_env) {
                    for (char* p = norm_env; *p; p++) {
                        if (*p == '\r' || *p == '\n' || *p == '\t') {
                            *p = ' ';
                        }
                    }
                }
            }

            char*       combined_args = NULL;
            const char* eff_args = NULL;
            if (def_args && def_args[0] != '\0' && args && args[0] != '\0') {
                size_t alen = strlen(def_args) + strlen(args) + 2;
                combined_args = malloc(alen);
                if (combined_args) {
                    snprintf(combined_args, alen, "%s %s", def_args, args);
                    eff_args = combined_args;
                } else {
                    eff_args = args;
                }
            } else if (args && args[0] != '\0') {
                eff_args = args;
            } else if (def_args && def_args[0] != '\0') {
                eff_args = def_args;
            }

            csilk_skill_exec_cfg_t cfg = {.root = (char*)(root && root[0] ? root : "/"),
                                          .timeout_ms = timeout > 0 ? timeout : 5000,
                                          .max_output_bytes = max_out > 0 ? max_out : 65536,
                                          .env = norm_env};
            csilk_ai_skill_set_exec_cfg(csk, &cfg);
            char* exec_out = NULL;
            int   rc = csilk_skill_exec_local(csk, path, eff_args, 0, &exec_out);
            csilk_ai_skill_free(csk);
            free(norm_env);
            free(combined_args);

            csilk_json_add_string(res, "status", rc == 0 ? "success" : "failed");
            csilk_json_add_string(res, "mode", "l3");
            csilk_json_add_string(res, "skill_name", name ? name : "");
            csilk_json_add_number(res, "rc", rc);
            if (exec_out) {
                csilk_json_t* parsed = csilk_json_parse(exec_out);
                if (parsed) {
                    csilk_json_add_object(res, "exec_result", parsed);
                } else {
                    csilk_json_add_string(res, "output", exec_out);
                }
                free(exec_out);
            }
        } else {
            csilk_json_add_string(res, "status", "failed");
            csilk_json_add_string(res, "mode", "l3");
            csilk_json_add_string(res, "skill_name", name ? name : "");
            csilk_json_add_number(res, "rc", -1);
            csilk_json_add_string(res, "error", "Failed to initialize L3 skill runtime");
        }
    } else {
        // Pure L2 Prompt Inspection
        csilk_json_add_string(res, "status", "success");
        csilk_json_add_string(res, "mode", "l2");
        csilk_json_add_string(res, "skill_name", name ? name : "");
        csilk_json_add_bool(res, "valid", true);
        size_t l2_len = l2 ? strlen(l2) : 0;
        csilk_json_add_number(res, "char_count", (double)l2_len);
        csilk_json_add_number(res, "token_estimate", (double)(l2_len / 4 + 1));
        csilk_json_add_string(res, "instructions", l2 ? l2 : "");
        csilk_json_add_string(res, "instructions_preview", l2 ? l2 : "");
    }

    csilk_json_free(sk);
    *out_res = res;
    return 0;
}

int
skill_usecase_seed_defaults(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }

    for (size_t i = 0; i < S_DEFAULT_SKILLS_COUNT; i++) {
        const mf_skill_t* def = &S_DEFAULT_SKILLS[i];
        csilk_json_t* existing = skill_repo_get_by_name((csilk_db_pool_t*)pool, user_id, def->name);
        if (existing) {
            csilk_json_free(existing);
            continue; // Already seeded, preserve user state
        }
        skill_repo_insert((csilk_db_pool_t*)pool, user_id, def);
    }
    return 0;
}

int
skill_usecase_reset_defaults(void* pool, int64_t user_id)
{
    if (!pool || user_id <= 0) {
        return -1;
    }

    for (size_t i = 0; i < S_DEFAULT_SKILLS_COUNT; i++) {
        const mf_skill_t* def = &S_DEFAULT_SKILLS[i];
        csilk_json_t* existing = skill_repo_get_by_name((csilk_db_pool_t*)pool, user_id, def->name);
        if (existing) {
            int64_t id = (int64_t)db_get_num(existing, "id");
            csilk_json_free(existing);
            skill_repo_update((csilk_db_pool_t*)pool, user_id, id, def);
        } else {
            skill_repo_insert((csilk_db_pool_t*)pool, user_id, def);
        }
    }
    return 0;
}

#define MF_SKILL_IMPORT_MAX_BYTES (256 * 1024)

int
skill_usecase_import_markdown(void* pool, int64_t user_id, const char* md_content, int64_t* out_id)
{
    if (!pool || !md_content) {
        return -1;
    }
    size_t content_len = strlen(md_content);
    if (content_len == 0 || content_len > MF_SKILL_IMPORT_MAX_BYTES) {
        return 1002;
    }

    // Parse YAML frontmatter: must start with "---\n" or "---\r\n"
    if (strncmp(md_content, "---", 3) != 0) {
        return 1002; // invalid format
    }
    const char* start = md_content + 3;
    while (*start == '\r' || *start == '\n') {
        start++;
    }

    const char* end = strstr(start, "\n---");
    if (!end) {
        end = strstr(start, "\r\n---");
    }
    if (!end) {
        return 1002; // no closing frontmatter
    }

    size_t fm_len = end - start;
    char*  fm = malloc(fm_len + 1);
    if (!fm) {
        return -1;
    }
    memcpy(fm, start, fm_len);
    fm[fm_len] = '\0';

    // Move past the closing ---
    const char* body = end + 4;
    while (*body == '-' || *body == '\r' || *body == '\n') {
        body++;
    }

    mf_skill_create_dto_t dto = {0};
    dto.enabled = 1;
    strncpy(dto.category, "finance", sizeof(dto.category) - 1);
    dto.instructions_l2 = body;

    // Line-by-line simple YAML key: value parser (thread-safe strtok_r)
    char* saveptr = NULL;
    char* line = strtok_r(fm, "\n", &saveptr);
    while (line) {
        while (*line == ' ' || *line == '\t' || *line == '\r') {
            line++;
        }
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char* key = line;
            char* val = colon + 1;
            while (*val == ' ' || *val == '\t') {
                val++;
            }
            // Strip trailing \r / quotes
            size_t vlen = strlen(val);
            while (vlen > 0 && (val[vlen - 1] == '\r' || val[vlen - 1] == ' ' ||
                                val[vlen - 1] == '"' || val[vlen - 1] == '\'')) {
                val[vlen - 1] = '\0';
                vlen--;
            }
            if (*val == '"' || *val == '\'') {
                val++;
            }

            if (strcmp(key, "name") == 0) {
                strncpy(dto.name, val, sizeof(dto.name) - 1);
            } else if (strcmp(key, "description") == 0) {
                strncpy(dto.description, val, sizeof(dto.description) - 1);
            } else if (strcmp(key, "license") == 0) {
                strncpy(dto.license, val, sizeof(dto.license) - 1);
            } else if (strcmp(key, "compatibility") == 0) {
                strncpy(dto.compatibility, val, sizeof(dto.compatibility) - 1);
            } else if (strcmp(key, "category") == 0) {
                strncpy(dto.category, val, sizeof(dto.category) - 1);
            } else if (strcmp(key, "tags") == 0) {
                strncpy(dto.tags, val, sizeof(dto.tags) - 1);
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(fm);

    return skill_usecase_create(pool, user_id, &dto, out_id);
}

int
skill_usecase_export_markdown(void* pool, int64_t user_id, int64_t id, char** out_md)
{
    if (!pool || !out_md) {
        return -1;
    }
    *out_md = NULL;

    csilk_json_t* sk = skill_repo_get((csilk_db_pool_t*)pool, user_id, id);
    if (!sk) {
        return 1002;
    }

    const char* name = csilk_json_get_string(sk, "name");
    const char* desc = csilk_json_get_string(sk, "description");
    const char* lic = csilk_json_get_string(sk, "license");
    const char* comp = csilk_json_get_string(sk, "compatibility");
    const char* cat = csilk_json_get_string(sk, "category");
    const char* tags = csilk_json_get_string(sk, "tags");
    const char* l2 = csilk_json_get_string(sk, "instructions_l2");

    size_t est_size = 256 + (name ? strlen(name) : 0) + (desc ? strlen(desc) : 0) +
                      (lic ? strlen(lic) : 0) + (comp ? strlen(comp) : 0) +
                      (cat ? strlen(cat) : 0) + (tags ? strlen(tags) : 0) + (l2 ? strlen(l2) : 0);
    char* buf = malloc(est_size);
    if (!buf) {
        csilk_json_free(sk);
        return -1;
    }

    snprintf(buf,
             est_size,
             "---\n"
             "name: %s\n"
             "description: \"%s\"\n"
             "license: %s\n"
             "compatibility: \"%s\"\n"
             "category: %s\n"
             "tags: %s\n"
             "---\n\n"
             "%s",
             name ? name : "",
             desc ? desc : "",
             lic && lic[0] ? lic : "MIT",
             comp && comp[0] ? comp : "minefolio >= 1.0",
             cat && cat[0] ? cat : "finance",
             tags && tags[0] ? tags : "",
             l2 ? l2 : "");

    csilk_json_free(sk);
    *out_md = buf;
    return 0;
}
