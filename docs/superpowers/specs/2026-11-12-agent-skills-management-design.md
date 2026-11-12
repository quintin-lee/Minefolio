# Standard Agent Skills Management Design

- **Status**: Ready for Approval
- **Date**: 2026-11-12
- **Author**: Antigravity Assistant & Engineering Team
- **Target Components**: Frontend SPA (`frontend/`), Backend C23 DDD Core (`backend/`), csilk AI Runtime Bridge

---

## 1. Intent & The Why

### 1.1 Motivation
Minefolio incorporates an advanced AI financial runtime (chat, workflows, MCP tools). Anthropic's **Agent Skills** specification introduces a standard three-level progressive-disclosure pattern:
- **L1 Metadata**: Name, description, license, environment compatibility (always available in system context / tool discovery).
- **L2 Instructions**: Detailed reasoning rules and procedural guides (`SKILL.md` Markdown body, loaded only when invoked).
- **L3 Resources / Execution**: Bundled executable scripts and resources running in bounded local execution or remote workers.

csilk has now implemented native `csilk_ai_skill_t` support (`include/csilk/drivers/ai/skill.h`), including progressive disclosure, `use_skill` tool discovery injection, and bounded local process execution (`csilk_skill_exec_local`).

Minefolio requires a **first-class Skill Management experience**:
1. Users can visually manage, create, edit, test, and toggle skills.
2. The backend provides a compliant DDD implementation for skill persistence, lifecycle, and runtime registration into AI sessions and workflows.
3. System ships with 4 out-of-the-box financial domain skills.
4. Support for importing and exporting standard `SKILL.md` bundles.

---

## 2. Goals, Non-Goals, & Anti-Goals

### 2.1 Goals
- **Standard Conformance**: 100% compliant with Anthropic Agent Skills frontmatter specification (kebab-case name, description with what & when, L2 instructions, L3 bounded local execution).
- **First-Class Frontend UI**: Dedicated `/skills` route under the `AI 智能` navigation group featuring a modern grid/list gallery, category/status filters, creation/edit drawer, interactive test runner, and `SKILL.md` import/export.
- **DDD Backend Architecture**: Strict adherence to Minefolio's 4-layer DDD model (`domain/skill`, `application/skill`, `infrastructure/repositories/skill_repo_impl.c`, `interfaces/http/controllers/skill_controller.c`).
- **Dual-Database Support**: Identical schema migrations for both SQLite (`V022__ai_skills.sql`) and PostgreSQL (`V022__ai_skills.sql`).
- **Seeded Financial Domain Skills**: 4 pre-packaged default skills seeded upon initialization:
  1. `financial-health-auditor`: Multi-dimensional cash flow, debt ratio, and emergency fund auditor.
  2. `dca-rebalance-advisor`: DCA plan optimizer and target allocation rebalancing advisor.
  3. `impulse-expense-guard`: Psychological budget defender and purchase necessity challenger.
  4. `receipt-tax-classifier`: Automated invoice/receipt VAT, business expense, and tax deduction classifier.
- **Testing & Verification**: Comprehensive unit test suite in CTest (`test_domain_skill`, `test_skill_repository`) + integration tests in `test_skills.sh` + inclusion in `test_link.sh`.

### 2.2 Non-Goals
- Remote cluster execution via `csilk_mq` (`CSILK_SKILL_REMOTE`) is reserved for distributed cloud deployments and is not required for standard self-hosted Minefolio instances (defaults to `CSILK_SKILL_LOCAL`).
- Writing a custom kernel sandbox (seccomp); local execution relies on csilk's bounded `fork` + confined `root` + timeouts + output caps.

### 2.3 Anti-Goals
- Embedding skill definitions as rigid hardcoded C strings without DB persistence or user-level customizability.
- Breaking backwards compatibility with existing AI chat and workflow endpoints.

---

## 3. Approaches Considered & Trade-offs

### Approach 1: Dedicated Top-Level Route `/skills` (Selected)
- **Trade-off**: Requires dedicated navigation entry and full-page layout, but provides the richest user experience: visual gallery, quick search, status badges, one-click test execution, tabbed editor for L1/L2/L3, and import/export.
- **Decision**: Selected by user. Fits cleanly alongside `/chat` and `/ai-traces` under `navGroups.ai`.

### Approach 2: Sub-tab inside Settings Center (`/settings?tab=skills`)
- **Trade-off**: Saves a top-level route, but severely constrains available screen real estate. Inspecting markdown instructions or reviewing test execution logs in a settings tab is cramped.
- **Decision**: Rejected in favor of Approach 1.

---

## 4. Detailed Specification

### 4.1 Database Schema (Migration V022)

Files:
- `backend/sql/migrations/sqlite/V022__ai_skills.sql`
- `backend/sql/migrations/postgres/V022__ai_skills.sql`

```sql
CREATE TABLE IF NOT EXISTS ai_skills (
    id INTEGER PRIMARY KEY AUTOINCREMENT, -- BIGSERIAL in Postgres
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

### 4.2 Runtime Invocation & Tool Calling Contract (`use_skill`)

1. **L1 Discovery**: When an AI chat or workflow begins, `skill_tool.c` gathers all enabled skills for `user_id`. It injects a meta-tool named `use_skill` into the AI context:
   ```json
   {
     "name": "use_skill",
     "description": "Trigger a specialized domain capability. Available skills:\n- financial-health-auditor: Audits overall financial health...\n- dca-rebalance-advisor: Analyzes asset allocation...",
     "parameters": {
       "type": "object",
       "properties": {
         "skill_name": { "type": "string", "enum": ["financial-health-auditor", "dca-rebalance-advisor", "..."] },
         "resource_path": { "type": "string", "description": "Optional L3 script/resource relative path" },
         "arguments": { "type": "string", "description": "Optional arguments for L3 execution" }
       },
       "required": ["skill_name"]
     }
   }
   ```
2. **L2 Execution (Prompt Return)**: When the model calls `use_skill(skill_name: "financial-health-auditor")`, the tool returns the L2 Markdown instructions (`instructions_l2`) as the tool call result. The LLM absorbs these specialized instructions in the next turn and continues solving the user's task.
3. **L3 Script Execution (Local Bounded Process)**: If `resource_path` is specified and the skill configures an L3 executable script, the tool invokes csilk's `csilk_skill_exec_local` and returns `{ "exit_code": 0, "stdout": "...", "truncated": false }`.

### 4.3 Security & Sandbox Boundary for L3 Execution
- **Path Confinement**: `exec_path` must be a relative path resolved strictly inside `exec_root`. csilk's `csilk_skill_exec_local` strictly rejects absolute paths, `..` directory traversal, and newlines.
- **Privilege Separation**: Only users with admin privileges may configure or edit L3 execution scripts (`exec_root`, `exec_path`). Standard users can toggle and use admin-configured skills.
- **Resource Limits**: Hard timeout (SIGKILL after `timeout_ms`, default 30s) and stdout cap (`max_output_bytes`, default 64KB).

### 4.4 Test Runner Semantics (`POST /api/ai/skills/:id/test`)
- **L3 Script Skills**: Executes the local script under sandbox bounds and returns `{ "mode": "l3", "exit_code": 0, "stdout": "...", "duration_ms": 42 }`.
- **L2 Prompt-Only Skills**: Performs frontmatter and Markdown syntax validation, expands dynamic placeholders, and returns `{ "mode": "l2", "valid": true, "token_estimate": 320, "instructions_preview": "..." }`. Optionally supports sending a test query via `ai_runtime_execute` to preview LLM output.

### 4.5 Standard `SKILL.md` File Format & Import/Export
- Single-file standard `SKILL.md` with YAML frontmatter:
  ```markdown
  ---
  name: financial-health-auditor
  description: "Audits overall financial health, emergency fund sufficiency, and debt-to-income ratios."
  license: MIT
  compatibility: "minefolio >= 1.0"
  category: finance
  tags: [audit, cashflow, debt]
  ---
  # Financial Health Auditor
  Detailed Markdown instructions...
  ```
- Parser handles standard Anthropic fields (`name`, `description`, `license`, `compatibility`) and Minefolio extensions (`category`, `tags`). Missing fields fall back to sensible defaults.

### 4.6 4 Seeded Built-in Financial Skills

1. **`financial-health-auditor`**
   - Category: `audit`
   - Description: "Audits overall financial health, emergency fund sufficiency (3-6x monthly expenses), and debt-to-income ratios."
   - L2 Instructions: Step-by-step diagnostic workflow checking liquid assets, liabilities, and monthly run rate with grading metrics (A/B/C/D).

2. **`dca-rebalance-advisor`**
   - Category: `investment`
   - Description: "Analyzes asset allocation deviations against target weights and suggests DCA rebalancing contributions."
   - L2 Instructions: Portfolio risk parity analysis, cash deployment guidelines, and buy-low allocation math.

3. **`impulse-expense-guard`**
   - Category: `budgeting`
   - Description: "Interrogates potential discretionary expenditures, calculates hourly-wage equivalent cost, and enforces cool-off rules."
   - L2 Instructions: Socratic questioning framework for non-essential purchases, alternative investment opportunity cost comparison.

4. **`receipt-tax-classifier`**
   - Category: `tax`
   - Description: "Classifies expense receipts and invoice line items for deductible business expenses, VAT deduction, or personal allowances."
   - L2 Instructions: Structured tax deduction rulebook and category mapping.

### 4.7 Built-in Skills Lifecycle & Reset
- **Seeding**: `skill_usecase_seed_defaults(pool, user_id)` is idempotent and called on user setup (`auth/usecases.c`) and during `V022` backfill.
- **Modification**: Built-in skills can be edited or disabled by the user (`is_builtin = 1`).
- **Reset**: "重置内置技能 (Reset Defaults)" re-syncs the 4 default skills to standard initial content without deleting custom user-created skills.

---

## 5. Frontend UI Layout & Components

- **Route**: `/skills` (`frontend/src/views/Skills.vue`)
- **Navigation**: Under `navGroups.ai` in `Layout.vue` with icon `ph:magic-wand-duotone`.
- **UI Structure**:
  - Top Action Header: Search, Category Filter (`全部`, `财务体检`, `投资理财`, `预算风控`, `税务记账`, `自定义`), New Skill, Import SKILL.md, Reset Defaults.
  - Card Gallery: Card with icon, kebab-case badge, category tag, description, enable/disable switch, action dropdown.
  - Multi-tab Dialog (`SkillDialog.vue`):
    - Tab 1: L1 Metadata
    - Tab 2: L2 Instructions (Monaco/Markdown editor)
    - Tab 3: L3 Local Execution
    - Tab 4: Interactive Test Runner
  - Import Dialog (`SkillImportDialog.vue`): Drag & drop `.md` with live frontmatter preview.
