# Agent Skills Management Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a first-class standard Agent Skill management capability in Minefolio, comprising a dedicated `/skills` management page on the Vue 3 frontend, a complete 4-layer DDD backend with dual SQLite/PostgreSQL migrations (`V022`), 4 seeded financial domain skills, `SKILL.md` import/export, and integration with csilk's progressive-disclosure `use_skill` discovery runtime.

**Architecture:** 
1. Database migration `V022__ai_skills.sql` (SQLite & PostgreSQL) stores skills with L1 metadata, L2 instructions, and L3 local execution bounds.
2. DDD 4-layer backend (`domain/skill`, `application/skill`, `infrastructure/repositories/skill_repo_impl.c`, `interfaces/http/controllers/skill_controller.c`) handles validation, CRUD, testing, import/export, and lazy default seeding.
3. Service layer bridge integrates enabled skills into AI chat/workflow sessions via csilk `use_skill` tool discovery.
4. Vue 3 SPA adds `/skills` with gallery view, status filters, multi-tab configuration drawer (L1/L2/L3/Test), and drag & drop `SKILL.md` parser.

**Tech Stack:** C23, csilk HTTP & AI framework, SQLite/PostgreSQL, Vue 3, TypeScript, Pinia, Element Plus, Iconify.

**Spec:** `docs/superpowers/specs/2026-11-12-agent-skills-management-design.md`

## Global Constraints

- Backend must follow strict DDD four-layer architecture (`main.c` -> controllers -> application -> domain/infrastructure).
- Use cases must use `void* pool` signature for infrastructure decoupling.
- ALL SQL queries must use parameterized prepared statements (`?` placeholders) and `NULLIF(CAST(? AS BIGINT), 0)` for nullable foreign keys/integers.
- Entity names must adhere to Anthropic kebab-case naming: `^[a-z0-9]+(-[a-z0-9]+)*$`, 1–64 characters.
- Multi-step DB writes must run inside `db_tx_scope_begin()` / `db_tx_scope_commit()`.
- Frontend must compile cleanly with `npm run build` (`vue-tsc -b && vite build`) with zero TypeScript or styling warnings.
- All backend C code must compile cleanly with zero warnings (`-Wall -Wextra -Werror`).

## Review Focus

1. **Duplicate Skill Name per User**: User attempts to create a skill with an existing name -> must return `code=1004` ("技能名称已存在").
2. **Invalid Kebab-case Name**: User supplies uppercase, special characters, leading/trailing hyphens, or consecutive hyphens -> must return `code=1002` validation error.
3. **L3 Process Traversal Guard**: User provides `exec_path` with `../` or an absolute path -> must reject with `code=1002` ("非法脚本路径，禁止目录穿越").
4. **Built-in Skill Protection on Reset**: User modifies built-in skills and adds custom skills, then invokes "Reset Defaults" -> must restore the 4 built-in skills to initial state while preserving custom skills.
5. **Standard `SKILL.md` Round-Trip**: Exporting a skill to `SKILL.md` and importing it into a clean environment must faithfully preserve name, description, license, category, and L2 instructions.

---

### Task 1: Database Migration V022 (`ai_skills`)

**Files:**
- Create: `backend/sql/migrations/sqlite/V022__ai_skills.sql`
- Create: `backend/sql/migrations/postgres/V022__ai_skills.sql`

**Interfaces:**
- Produces: `ai_skills` table schema with indexes `idx_ai_skills_user_enabled` and `idx_ai_skills_category`.

- [ ] **Step 1: Write SQLite migration `V022__ai_skills.sql`**
```sql
CREATE TABLE IF NOT EXISTS ai_skills (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    description TEXT NOT NULL,
    license TEXT,
    compatibility TEXT,
    category TEXT NOT NULL DEFAULT 'finance',
    tags TEXT,
    enabled INTEGER NOT NULL DEFAULT 1,
    instructions_l2 TEXT NOT NULL,
    backend TEXT NOT NULL DEFAULT 'local' CHECK (backend IN ('local', 'remote')),
    exec_root TEXT,
    exec_path TEXT,
    exec_args TEXT,
    exec_env TEXT,
    timeout_ms INTEGER NOT NULL DEFAULT 30000,
    max_output_bytes INTEGER NOT NULL DEFAULT 65536,
    is_builtin INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (user_id, name)
);

CREATE INDEX IF NOT EXISTS idx_ai_skills_user_enabled ON ai_skills(user_id, enabled);
CREATE INDEX IF NOT EXISTS idx_ai_skills_category ON ai_skills(user_id, category);
```

- [ ] **Step 2: Write PostgreSQL migration `V022__ai_skills.sql`**
```sql
CREATE TABLE IF NOT EXISTS ai_skills (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name VARCHAR(64) NOT NULL,
    description VARCHAR(1024) NOT NULL,
    license VARCHAR(64),
    compatibility VARCHAR(500),
    category VARCHAR(64) NOT NULL DEFAULT 'finance',
    tags TEXT,
    enabled SMALLINT NOT NULL DEFAULT 1,
    instructions_l2 TEXT NOT NULL,
    backend VARCHAR(32) NOT NULL DEFAULT 'local' CHECK (backend IN ('local', 'remote')),
    exec_root TEXT,
    exec_path TEXT,
    exec_args TEXT,
    exec_env TEXT,
    timeout_ms INTEGER NOT NULL DEFAULT 30000,
    max_output_bytes INTEGER NOT NULL DEFAULT 65536,
    is_builtin SMALLINT NOT NULL DEFAULT 0,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (user_id, name)
);

CREATE INDEX IF NOT EXISTS idx_ai_skills_user_enabled ON ai_skills(user_id, enabled);
CREATE INDEX IF NOT EXISTS idx_ai_skills_category ON ai_skills(user_id, category);
```

- [ ] **Step 3: Verify migration applies with csilk migration engine**
Run: `ctest --test-dir backend/build -R test_migration_engine --output-on-failure`
Expected: PASS

- [ ] **Step 4: Commit**
```bash
git add backend/sql/migrations/
git commit -m "feat(db): add V022 migration for ai_skills table"
```

---

### Task 2: Domain Layer (`domain/skill`)

**Files:**
- Create: `backend/src/domain/skill/entity.h`
- Create: `backend/src/domain/skill/rules.h`
- Create: `backend/src/domain/skill/rules.c`
- Create: `backend/src/domain/skill/repository.h`
- Create: `backend/tests/unit/test_domain_skill.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `mf_skill_t`, `mf_skill_meta_t`, `mf_skill_exec_cfg_t`
  - `int mf_skill_validate_name(const char* name)`
  - `int mf_skill_validate(const mf_skill_t* skill, char* err_buf, size_t err_size)`
  - `int mf_skill_validate_exec_path(const char* path)`
  - `mf_skill_repo_t` interface

- [ ] **Step 1: Write failing domain unit test `backend/tests/unit/test_domain_skill.c`**
```c
#include <assert.h>
#include <string.h>
#include "domain/skill/entity.h"
#include "domain/skill/rules.h"

int main(void) {
    // Valid kebab-case name
    assert(mf_skill_validate_name("financial-analyzer") == 0);
    assert(mf_skill_validate_name("dca-advisor-v2") == 0);

    // Invalid kebab-case names
    assert(mf_skill_validate_name("FinancialAnalyzer") != 0); // uppercase
    assert(mf_skill_validate_name("-leading-hyphen") != 0);
    assert(mf_skill_validate_name("trailing-hyphen-") != 0);
    assert(mf_skill_validate_name("consecutive--hyphen") != 0);
    assert(mf_skill_validate_name("") != 0);

    // Path traversal check
    assert(mf_skill_validate_exec_path("scripts/run.sh") == 0);
    assert(mf_skill_validate_exec_path("../etc/passwd") != 0);
    assert(mf_skill_validate_exec_path("/bin/sh") != 0);

    // Full entity validation
    mf_skill_t sk = {0};
    strncpy(sk.name, "valid-skill", sizeof(sk.name));
    strncpy(sk.description, "A test description of what and when.", sizeof(sk.description));
    sk.instructions_l2 = "## Instructions\nDo something.";
    char err[256] = {0};
    assert(mf_skill_validate(&sk, err, sizeof(err)) == 0);

    return 0;
}
```

- [ ] **Step 2: Register test in `backend/CMakeLists.txt` and run to verify it fails**
Run: `cmake --build backend/build --target test_domain_skill`
Expected: FAIL (missing headers/functions)

- [ ] **Step 3: Implement domain entity, rules, and repository contract**
- `backend/src/domain/skill/entity.h`: defines data structs.
- `backend/src/domain/skill/rules.h/.c`: implements validation rules for name, path traversal, description length.
- `backend/src/domain/skill/repository.h`: defines repository operations.

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir backend/build -R test_domain_skill --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**
```bash
git add backend/src/domain/skill/ backend/tests/unit/test_domain_skill.c backend/CMakeLists.txt
git commit -m "feat(domain): add skill domain entities, validation rules, and repository contract"
```

---

### Task 3: Infrastructure Repository (`skill_repo_impl`)

**Files:**
- Create: `backend/src/infrastructure/repositories/skill_repo_impl.h`
- Create: `backend/src/infrastructure/repositories/skill_repo_impl.c`
- Create: `backend/tests/unit/test_skill_repository.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
- Consumes: `domain/skill/entity.h`, `csilk/csilk.h`, `common/db.h`
- Produces:
  - `csilk_json_t* skill_repo_list(csilk_db_pool_t* pool, int64_t user_id, const char* category, int enabled_only)`
  - `csilk_json_t* skill_repo_get(csilk_db_pool_t* pool, int64_t user_id, int64_t id)`
  - `csilk_json_t* skill_repo_get_by_name(csilk_db_pool_t* pool, int64_t user_id, const char* name)`
  - `int64_t skill_repo_insert(csilk_db_pool_t* pool, int64_t user_id, const mf_skill_t* skill)`
  - `int skill_repo_update(csilk_db_pool_t* pool, int64_t user_id, int64_t id, const mf_skill_t* skill)`
  - `int skill_repo_delete(csilk_db_pool_t* pool, int64_t user_id, int64_t id)`
  - `int skill_repo_toggle(csilk_db_pool_t* pool, int64_t user_id, int64_t id, int enabled)`

- [ ] **Step 1: Write failing unit test `backend/tests/unit/test_skill_repository.c`**
Test CRUD: insert a skill, get by ID, list by category, toggle enabled, update, delete, verify unique constraint failure on duplicate name.

- [ ] **Step 2: Register test in `backend/CMakeLists.txt` and verify it fails**
Run: `cmake --build backend/build --target test_skill_repository`
Expected: FAIL

- [ ] **Step 3: Implement `skill_repo_impl.c`**
Implement all SQL operations with `csilk_db_query_param_json` and prepared statement placeholders (`?`).

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir backend/build -R test_skill_repository --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**
```bash
git add backend/src/infrastructure/repositories/skill_repo_impl* backend/tests/unit/test_skill_repository.c backend/CMakeLists.txt
git commit -m "feat(repo): implement skill repository with SQL prepared statements"
```

---

### Task 4: Application Layer (`application/skill`) & Builtin Financial Skills

**Files:**
- Create: `backend/src/application/skill/dtos.h`
- Create: `backend/src/application/skill/usecases.h`
- Create: `backend/src/application/skill/usecases.c`
- Modify: `backend/src/application/auth/usecases.c` (call `skill_usecase_seed_defaults`)

**Interfaces:**
- Consumes: `domain/skill/entity.h`, `infrastructure/repositories/skill_repo_impl.h`
- Produces:
  - `int skill_usecase_list(void* pool, int64_t user_id, const char* category, int enabled_only, csilk_json_t** out_data)`
  - `int skill_usecase_get(void* pool, int64_t user_id, int64_t id, csilk_json_t** out_data)`
  - `int skill_usecase_create(void* pool, int64_t user_id, const mf_skill_create_dto_t* dto, int64_t* out_id)`
  - `int skill_usecase_update(void* pool, int64_t user_id, int64_t id, const mf_skill_update_dto_t* dto)`
  - `int skill_usecase_delete(void* pool, int64_t user_id, int64_t id)`
  - `int skill_usecase_toggle(void* pool, int64_t user_id, int64_t id, int enabled)`
  - `int skill_usecase_test_execute(void* pool, int64_t user_id, int64_t id, const char* args, csilk_json_t** out_res)`
  - `int skill_usecase_seed_defaults(void* pool, int64_t user_id)`
  - `int skill_usecase_import_markdown(void* pool, int64_t user_id, const char* md_content, int64_t* out_id)`
  - `int skill_usecase_export_markdown(void* pool, int64_t user_id, int64_t id, char** out_md)`

- [ ] **Step 1: Define DTOs in `backend/src/application/skill/dtos.h`**
Define create/update/test DTO structs.

- [ ] **Step 2: Implement 4 Seeded Financial Domain Skills in `usecases.c`**
Embed default templates for:
1. `financial-health-auditor`
2. `dca-rebalance-advisor`
3. `impulse-expense-guard`
4. `receipt-tax-classifier`
Ensure `skill_usecase_seed_defaults` is idempotent (checks `skill_repo_get_by_name` before insert).

- [ ] **Step 3: Implement Markdown Frontmatter Parser / Serializer**
Parse YAML frontmatter (`--- ... ---`) and extract `name`, `description`, `license`, `compatibility`, `category`, `tags`, plus Markdown body.

- [ ] **Step 4: Implement Test Runner Logic**
- For L3 skills: call `csilk_skill_exec_local` under bounded parameters.
- For L2 skills: validate frontmatter + return token estimation and instructions preview.

- [ ] **Step 5: Integrate default seeding into `auth/usecases.c`**
Add `skill_usecase_seed_defaults(pool, user_id)` at user registration.

- [ ] **Step 6: Commit**
```bash
git add backend/src/application/skill/ backend/src/application/auth/usecases.c
git commit -m "feat(application): add skill use cases, 4 seeded financial skills, and markdown import/export"
```

---

### Task 5: HTTP Controller & Route Registration

**Files:**
- Create: `backend/src/interfaces/http/controllers/skill_controller.h`
- Create: `backend/src/interfaces/http/controllers/skill_controller.c`
- Modify: `backend/src/main.c`

**Interfaces:**
- Produces:
  - `void register_skill_routes(csilk_app_t* app)`
  - REST endpoints under `/api/ai/skills`

- [ ] **Step 1: Implement `skill_controller.c`**
Expose:
- `GET /api/ai/skills` -> `skill_controller_list`
- `GET /api/ai/skills/:id` -> `skill_controller_get`
- `POST /api/ai/skills` -> `skill_controller_create`
- `PUT /api/ai/skills/:id` -> `skill_controller_update`
- `DELETE /api/ai/skills/:id` -> `skill_controller_delete`
- `POST /api/ai/skills/:id/toggle` -> `skill_controller_toggle`
- `POST /api/ai/skills/:id/test` -> `skill_controller_test`
- `POST /api/ai/skills/import` -> `skill_controller_import`
- `GET /api/ai/skills/:id/export` -> `skill_controller_export`
- `POST /api/ai/skills/reset-defaults` -> `skill_controller_reset_defaults`

- [ ] **Step 2: Register routes in `backend/src/main.c`**
Add `#include "interfaces/http/controllers/skill_controller.h"` and call `register_skill_routes(app);`.

- [ ] **Step 3: Add Case 43 in `backend/tests/test_link.sh`**
Add integration test scenarios:
- Seeding check: default skills present.
- Create custom skill: verify 200 and database row.
- Kebab-case rejection: verify 1002 on invalid name.
- Duplicate name rejection: verify 1004.
- Toggle skill: enable/disable.
- Export SKILL.md: verify YAML frontmatter.
- Import SKILL.md: verify roundtrip.
- Test execution endpoint.

- [ ] **Step 4: Run `test_link.sh` to verify integration**
Run: `backend/tests/test_link.sh`
Expected: PASS (all cases including Case 43).

- [ ] **Step 5: Commit**
```bash
git add backend/src/interfaces/http/controllers/skill_controller* backend/src/main.c backend/tests/test_link.sh
git commit -m "feat(api): register skill HTTP endpoints and integration test in test_link.sh"
```

---

### Task 6: AI Runtime Bridge (`use_skill` Discovery Tool)

**Files:**
- Create: `backend/src/services/ai/tools/skill_tool.h`
- Create: `backend/src/services/ai/tools/skill_tool.c`
- Modify: `backend/src/services/ai/tools/registry.c`

**Interfaces:**
- Consumes: `application/skill/usecases.h`, `csilk/drivers/ai/skill.h`
- Produces:
  - `int ai_skill_register_tools(csilk_wf_t* wf, void* pool, int64_t user_id)`
  - Tool definition `use_skill` with auto-aggregated L1 descriptions.

- [ ] **Step 1: Implement `skill_tool.c`**
Dynamically query user's enabled skills and construct the `use_skill` tool definition with an enum of valid skill names.

- [ ] **Step 2: Implement `use_skill` execution callback**
When `use_skill` is dispatched:
- Read L2 instructions from DB.
- If L3 resource path provided and configured: execute via `csilk_skill_exec_local`.
- Return envelope to LLM context.

- [ ] **Step 3: Register in AI runtime tool registry**
Wire `use_skill` into `registry.c`.

- [ ] **Step 4: Test in `backend/tests/test_ai_tool_call.sh`**
Add scenario verifying the model receives `use_skill` in tool options.

- [ ] **Step 5: Commit**
```bash
git add backend/src/services/ai/tools/skill_tool* backend/src/services/ai/tools/registry.c backend/tests/test_ai_tool_call.sh
git commit -m "feat(ai): wire enabled skills into AI runtime discovery tool use_skill"
```

---

### Task 7: Frontend Types & API Client (`frontend/src/api/skills.ts`)

**Files:**
- Create: `frontend/src/types/skill.ts`
- Create: `frontend/src/api/skills.ts`
- Modify: `frontend/src/types/index.ts`

**Interfaces:**
- Produces:
  - TypeScript types: `AiSkill`, `SkillCategory`, `SkillCreatePayload`, `SkillUpdatePayload`, `SkillTestResult`
  - API methods: `getSkills()`, `getSkill()`, `createSkill()`, `updateSkill()`, `deleteSkill()`, `toggleSkill()`, `testSkill()`, `importSkill()`, `exportSkillUrl()`, `resetDefaultSkills()`

- [ ] **Step 1: Write `frontend/src/types/skill.ts`**
Define interfaces mirroring backend DTOs.

- [ ] **Step 2: Write `frontend/src/api/skills.ts`**
Implement Axios-wrapped API calls following standard Minefolio client patterns (`http.get`, `http.post`, `http.put`, `http.delete`).

- [ ] **Step 3: Export in `frontend/src/types/index.ts`**

- [ ] **Step 4: Commit**
```bash
git add frontend/src/types/skill.ts frontend/src/api/skills.ts frontend/src/types/index.ts
git commit -m "feat(frontend): add skill TypeScript interfaces and API client"
```

---

### Task 8: Frontend Skills Management View (`views/Skills.vue`) & Components

**Files:**
- Create: `frontend/src/views/Skills.vue`
- Create: `frontend/src/components/skills/SkillCard.vue`
- Create: `frontend/src/components/skills/SkillDialog.vue`
- Create: `frontend/src/components/skills/SkillImportDialog.vue`
- Modify: `frontend/src/router/index.ts`
- Modify: `frontend/src/views/Layout.vue`
- Modify: `frontend/src/utils/locale.ts`

**Interfaces:**
- Produces:
  - Responsive `/skills` route
  - Navigation item under `AI 智能`
  - Multi-tab configuration dialog (L1/L2/L3/Test)
  - Drag-and-drop import dialog

- [ ] **Step 1: Add i18n strings in `frontend/src/utils/locale.ts`**
Add keys for `nav.skills`, `skills.title`, `skills.desc`, `skills.newSkill`, `skills.importSkill`, `skills.resetDefaults`, `skills.l1Tab`, `skills.l2Tab`, `skills.l3Tab`, `skills.testTab`, etc.

- [ ] **Step 2: Add route in `frontend/src/router/index.ts`**
Register `{ path: 'skills', name: 'Skills', component: () => import('@/views/Skills.vue') }`.

- [ ] **Step 3: Add menu item in `frontend/src/views/Layout.vue`**
Insert `<el-menu-item index="/skills" @click="goTo('/skills')">` under `navGroups.ai` with icon `ph:magic-wand-duotone`.

- [ ] **Step 4: Build `SkillCard.vue`**
Card with icon, kebab-case badge, category tag, description, quick switch, and actions.

- [ ] **Step 5: Build `SkillDialog.vue`**
Drawer/modal with 4 tabs:
- L1 Metadata
- L2 Instructions (Monaco / textarea markdown editor with preview)
- L3 Execution (Admin-only process bounds)
- Tab 4: Interactive Test Runner

- [ ] **Step 6: Build `SkillImportDialog.vue`**
File upload / drag-and-drop for `.md` files with live parsing preview.

- [ ] **Step 7: Build `views/Skills.vue`**
Assemble filter bar, grid gallery, statistics counter, and dialog orchestrations.

- [ ] **Step 8: Verify build passes with zero errors**
Run: `npm --prefix frontend run build`
Expected: 0 errors (`vue-tsc -b && vite build` succeeds).

- [ ] **Step 9: Commit**
```bash
git add frontend/src/views/Skills.vue frontend/src/components/skills/ frontend/src/router/index.ts frontend/src/views/Layout.vue frontend/src/utils/locale.ts
git commit -m "feat(frontend): create Skills management page, components, and router integration"
```

---

### Task 9: Full End-to-End Verification & Build Validation

**Files:**
- Verify: Full backend build and CTest test suites
- Verify: Full frontend build
- Verify: Full integration test suite `test_link.sh`

- [ ] **Step 1: Run Backend CMake Build**
Run: `cmake --build backend/build --parallel`
Expected: 0 errors, 0 warnings.

- [ ] **Step 2: Run CTest Unit Suites**
Run: `ctest --test-dir backend/build --output-on-failure`
Expected: 100% tests passed (57/57).

- [ ] **Step 3: Run Full Integration Regression Suite**
Run: `backend/tests/test_link.sh`
Expected: PASS=200+ FAIL=0.

- [ ] **Step 4: Run Frontend TypeCheck & Production Build**
Run: `npm --prefix frontend run build`
Expected: 0 errors.

- [ ] **Step 5: Final Git Status Verification**
Run: `git status`
Expected: Clean working copy on `master`.
