# AI Context Compact Projection & Atomic Multi-Action Batch Protocol Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce LLM context token usage by over 60% through semantic result projection of read tools, and provide atomic multi-action batch proposals (`propose_batch_actions`) with HMAC nonced batch tokens and transactional commit (`POST /api/ai/actions/confirm-batch`).

**Architecture:** 
1. Semantic projection layer in `services/ai/tools/projection.c` filters out technical metadata (foreign keys, timestamps, internal audit fields) from read-only tool results before returning to LLM.
2. Composite multi-action HMAC signature protocol in `services/ai/policy/confirmation.c` computes deterministic hashes across heterogeneous action arrays (`daily_expense` and `transfer`) and single-use anti-replay nonces.
3. Unified `propose_batch_actions` tool validates action parameters and drafts multi-action transactions.
4. Atomic `POST /api/ai/actions/confirm-batch` endpoint executes all sub-actions inside a single `db_tx_scope_t` database transaction, rolling back all operations if any sub-action fails.

**Tech Stack:** C23, csilk HTTP framework, OpenSSL HMAC-SHA256, SQLite / PostgreSQL, 128-bit decimal fixed point arithmetic.

**Spec:** [`docs/superpowers/specs/2026-11-02-ledger-and-ai-workflow-engine-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-02-ledger-and-ai-workflow-engine-design.md) (Section 3: Milestone 3)

## Global Constraints

- **C23 Standard**: All C code must compile cleanly under C23 with `-Wall -Wextra -Werror` compliance.
- **Financial Precision**: All monetary values in batch actions must map directly to `money_t` 128-bit decimal fixed-point types.
- **Transactional Atomicity**: All sub-actions in a batch confirmation must execute in a single `db_tx_scope_t` transaction.
- **Security & Nonce Replay**: Every batch confirmation token must be single-use and bound to `user_id`, composite `actions_hash`, and expiration TTL.
- **Zero Regression**: All existing 48 CTest unit test suites and 40 `test_link.sh` integration cases must remain 100% green.

## Review Focus

1. **Information Loss Prevention**: Compact projection must preserve essential financial fields (`id`, `name`, `currency`, `balance`, `current_value`, `cost_basis`, `amount`, `category_name`) while stripping metadata.
2. **Partial Array Tampering**: Tampering with any field in any item of the batch actions array must cause HMAC signature or hash verification to fail.
3. **Rollback Integrity on Partial Failure**: If action 3 of 5 fails, actions 1 and 2 must be rolled back completely; zero partial database writes.
4. **Memory Hygiene**: JSON objects and heap strings generated during projection, hashing, and batch execution must be freed on all return paths.
5. **Anti-Replay**: Consuming a batch token twice must be rejected immediately with `AI_CONFIRM_ERR_REPLAY`.

---

## File Structure Map

- **Create**: `backend/src/services/ai/tools/projection.h` — Semantic projection interface for compact tool results.
- **Create**: `backend/src/services/ai/tools/projection.c` — Field filtering and array truncation implementations.
- **Modify**: `backend/src/services/ai/tools/dispatcher.c` — Integrate semantic projection before returning results to LLM.
- **Create**: `backend/src/services/ai/tools/batch_tool.h` — Propose batch actions tool interface.
- **Create**: `backend/src/services/ai/tools/batch_tool.c` — Propose batch actions tool implementation.
- **Modify**: `backend/src/services/ai/tools/registry.c` — Register `propose_batch_actions`.
- **Modify**: `backend/src/services/ai/policy/confirmation.h` — Composite batch hash and token validation signatures.
- **Modify**: `backend/src/services/ai/policy/confirmation.c` — Batch HMAC token generation and single-use verification.
- **Modify**: `backend/src/interfaces/http/controllers/ai_controller.h` — Route declarations for batch confirm.
- **Modify**: `backend/src/interfaces/http/controllers/ai_controller.c` — Implement `POST /api/ai/actions/confirm-batch`.
- **Create**: `backend/tests/unit/test_ai_token_compact.c` — Unit tests for tool result compaction.
- **Create**: `backend/tests/unit/test_ai_batch_confirmation.c` — Unit tests for batch token and atomic execution.
- **Modify**: `backend/CMakeLists.txt` — Register new tests and source files.

---

### Task 1: AI Tool Result Semantic Projection Layer

**Files:**
- Create: `backend/src/services/ai/tools/projection.h`
- Create: `backend/src/services/ai/tools/projection.c`
- Modify: `backend/src/services/ai/tools/dispatcher.c`
- Create: `backend/tests/unit/test_ai_token_compact.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
```c
#pragma once
#include "csilk/csilk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 对只读查询工具的原始输出执行面向大模型的语义精简投影
 * @param tool_name 工具名称 (如 "get_assets", "get_daily_expenses", "get_transactions")
 * @param raw_json 工具执行器返回的原生 JSON 字符串
 * @return char* 紧凑投影后的 JSON 字符串（调用方负责 free）
 */
char* ai_project_tool_result(const char* tool_name, const char* raw_json);

#ifdef __cplusplus
}
#endif
```

- [x] **Step 1: Write unit test `test_ai_token_compact.c`**
  - Verify `get_assets`: Retains `id`, `name`, `asset_type`, `currency`, `balance`, `current_value`, `cost_basis`. Removes `user_id`, `created_at`, `updated_at`, `note`, `icon`, `sort_order`, `quote_source`, `symbol`.
  - Verify `get_daily_expenses`: Limits array to max 15 items, retains `id`, `date`, `amount`, `category_name`, `asset_name`, `type`, `note`.
  - Verify `get_transactions`: Limits array to max 15 items, retains `id`, `transaction_date`, `transaction_type`, `amount`, `category_name`, `asset_name`, `note`.
  - Verify byte/token reduction is >= 60%.
  - Verify non-query tools (e.g. `web_search` or error payloads) are passed through un-altered.

- [x] **Step 2: Register in `backend/CMakeLists.txt` and verify RED failure**
  - Add `test_ai_token_compact` via `add_financial_unit_test`.
  - Run `cmake --build backend/build --target test_ai_token_compact` -> expected FAIL.

- [x] **Step 3: Implement `projection.h` and `projection.c`**
  - Implement selective field extraction and array slicing.
  - Integrate with `ai_tool_dispatch_parsed` in `dispatcher.c`: after `tool->execute(tool, ctx, normalized)`, if result is not an error, apply `ai_project_tool_result`.

- [x] **Step 4: Run test to verify GREEN pass**
  - Run `./backend/build/test_ai_token_compact` -> expected PASS.
  - Run `ctest --test-dir backend/build --output-on-failure`.

- [x] **Step 5: Commit Task 1**
  - `git commit -m "feat(ai): ⚡ implement semantic result projection for compact tool context"`

---

### Task 2: Batch Confirmation Draft & Multi-Action HMAC Token Protocol

**Files:**
- Modify: `backend/src/services/ai/policy/confirmation.h`
- Modify: `backend/src/services/ai/policy/confirmation.c`
- Create: `backend/tests/unit/test_ai_batch_confirmation.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
```c
void ai_confirmation_hash_batch_actions(const csilk_json_t* actions_array,
                                        char*               out_hash,
                                        size_t              out_hash_sz);

char* ai_confirmation_create_batch_token(int64_t             user_id,
                                         int64_t             session_id,
                                         const csilk_json_t* actions_array,
                                         ai_risk_level_t     risk,
                                         int                 ttl_seconds);

ai_confirmation_status_t ai_confirmation_verify_and_consume_batch(
    int64_t             user_id,
    int64_t             session_id,
    const csilk_json_t* actions_array,
    const char*         token);
```

- [x] **Step 1: Write unit tests in `test_ai_batch_confirmation.c`**
  - Test batch hash determinism for multiple actions.
  - Test batch token generation with `mf_batch_v1` prefix.
  - Test successful verification and single-use nonce consumption.
  - Test replay attack prevention: second verification fails with `AI_CONFIRM_ERR_REPLAY`.
  - Test tampering detection: altering `amount` or `asset_id` in any item fails with `AI_CONFIRM_ERR_ARGS_MISMATCH`.
  - Test user ID mismatch rejection (`AI_CONFIRM_ERR_USER_MISMATCH`).
  - Test expired token rejection (`AI_CONFIRM_ERR_EXPIRED`).

- [x] **Step 2: Register in `backend/CMakeLists.txt` and verify RED failure**
  - Run `cmake --build backend/build --target test_ai_batch_confirmation` -> expected FAIL.

- [x] **Step 3: Implement batch hashing and verification in `confirmation.c`**
  - Canonicalize JSON for each action in the array.
  - Compute composite SHA-256 hash across canonical action representations.
  - Implement `ai_confirmation_create_batch_token` and `ai_confirmation_verify_and_consume_batch`.

- [x] **Step 4: Run test to verify GREEN pass**
  - Run `./backend/build/test_ai_batch_confirmation` -> expected PASS.
  - Run `ctest --test-dir backend/build --output-on-failure`.

- [x] **Step 5: Commit Task 2**
  - `git commit -m "feat(ai): 🛡️ implement composite HMAC token protocol for multi-action batches"`

---

### Task 3: Propose Batch Actions Tool & Registry Integration

**Files:**
- Create: `backend/src/services/ai/tools/batch_tool.h`
- Create: `backend/src/services/ai/tools/batch_tool.c`
- Modify: `backend/src/services/ai/tools/registry.c`
- Modify: `backend/tests/unit/test_ai_tools.c`

**Interfaces:**
- Tool name: `"propose_batch_actions"`
- Schema:
  ```json
  {
    "type": "object",
    "properties": {
      "actions": {
        "type": "array",
        "description": "List of actions to propose",
        "items": {
          "type": "object",
          "properties": {
            "action_type": {"type": "string", "enum": ["daily_expense", "transfer"]},
            "amount": {"type": "number"},
            "type": {"type": "string", "enum": ["expense", "income"]},
            "category_id": {"type": "integer"},
            "asset_id": {"type": "integer"},
            "from_asset_id": {"type": "integer"},
            "to_asset_id": {"type": "integer"},
            "date": {"type": "string"},
            "note": {"type": "string"}
          },
          "required": ["action_type", "amount"]
        }
      }
    },
    "required": ["actions"]
  }
  ```

- [x] **Step 1: Add test cases to `test_ai_tools.c` for `propose_batch_actions`**
  - Test rejection on empty action list or > 20 actions.
  - Test rejection on invalid action types.
  - Test valid proposal returning structured summary and `mf_batch_v1` draft token.

- [x] **Step 2: Run test to verify RED failure**
  - Run `cmake --build backend/build --target test_ai_tools` -> expected FAIL.

- [x] **Step 3: Implement `batch_tool.h/.c` and register in `registry.c`**
  - Implement action array validation and asset ownership check.
  - Form human-readable summary ("1. 支出 45.00 CNY (午餐); 2. 转账 1000.00 CNY...").
  - Call `ai_confirmation_create_batch_token`.
  - Return JSON draft object.

- [x] **Step 4: Run test to verify GREEN pass**
  - Run `./backend/build/test_ai_tools` -> expected PASS.
  - Run `ctest --test-dir backend/build --output-on-failure`.

- [x] **Step 5: Commit Task 3**
  - `git commit -m "feat(ai): 📦 implement propose_batch_actions tool and schema registry"`

---

### Task 4: Atomic Batch Confirmation Endpoint & Execution Pipeline

**Files:**
- Modify: `backend/src/interfaces/http/controllers/ai_controller.h`
- Modify: `backend/src/interfaces/http/controllers/ai_controller.c`
- Modify: `backend/tests/unit/test_ai_batch_confirmation.c`

**Interfaces:**
- Route: `POST /api/ai/actions/confirm-batch`
- Request Body:
  ```json
  {
    "draft_token": "mf_batch_v1...",
    "actions": [
      {"action_type": "daily_expense", "amount": 45.0, "type": "expense", "category_id": 3, "asset_id": 1, "note": "午餐"},
      {"action_type": "transfer", "amount": 1000.0, "from_asset_id": 1, "to_asset_id": 2, "note": "还信用卡"}
    ]
  }
  ```
- Response:
  - Success (200): `{code: 0, message: "ok", data: {executed_count: 2, action_results: [...]}}`
  - Replay / Expired / Invalid (200): `{code: 1002 or 1004, message: "...", data: null}`

- [ ] **Step 1: Write integration tests in `test_ai_batch_confirmation.c`**
  - Test successful batch confirmation: 2 expenses + 1 transfer executed in a single transaction.
  - Verify asset balances updated correctly.
  - Test atomic rollback: create a batch where action 3 has an invalid asset ID. Verify actions 1 and 2 are rolled back completely.
  - Test replay attempt on already confirmed batch fails with error code.

- [ ] **Step 2: Run test to verify RED failure**
  - Run `cmake --build backend/build --target test_ai_batch_confirmation` -> expected FAIL.

- [ ] **Step 3: Implement `api_ai_action_confirm_batch_handler` in `ai_controller.c`**
  - Extract user ID from context.
  - Validate and consume batch token via `ai_confirmation_verify_and_consume_batch`.
  - Open transaction scope `db_tx_scope_begin(pool, "mf_confirm_batch", &scope)`.
  - Iterate through actions:
    - If `daily_expense`: execute through `ledger_apply_expense` (or expense usecase).
    - If `transfer`: execute through `ledger_apply_transfer` (or transfer usecase).
    - If any fails: `db_tx_scope_rollback(pool, &scope)` and return error.
  - If all succeed: `db_tx_scope_commit(pool, &scope)` and return success response.
  - Register route `POST /api/ai/actions/confirm-batch` in `register_ai_routes`.

- [ ] **Step 4: Run test to verify GREEN pass**
  - Run `./backend/build/test_ai_batch_confirmation` -> expected PASS.
  - Run `ctest --test-dir backend/build --output-on-failure`.

- [ ] **Step 5: Commit Task 4**
  - `git commit -m "feat(ai): ⚡ implement atomic batch confirmation endpoint with tx rollback"`

---

### Task 5: Full Integration Verification Gate & Regression Suite

**Files:**
- Test: Full CTest unit test suite (50 suites)
- Test: `backend/tests/test_link.sh` (add Case 41 for batch actions)
- Test: `backend/tests/test_ai_loop_hardening.sh`
- Test: `backend/tests/test_mcp.sh` & `backend/tests/test_mcp_server.sh`
- Build: Frontend production build
- Modify: `docs/superpowers/plans/2026-11-03-ai-compact-projection-and-batch-actions.md`

- [ ] **Step 1: Run complete CTest unit test suites**
  - Run: `ctest --test-dir backend/build --output-on-failure`
  - Target: 50/50 test suites passed (100%).

- [ ] **Step 2: Add Case 41 in `backend/tests/test_link.sh` and run integration test**
  - Case 41 tests:
    - Propose batch actions (expense + transfer) -> receives batch draft token.
    - Confirm batch actions via `POST /api/ai/actions/confirm-batch` -> returns code 0.
    - Assert expense and transfer recorded in database and balances updated.
    - Replay confirm batch with same token -> rejected with code 1004.
    - Tampered confirm batch -> rejected with code 1002.
  - Run: `./backend/tests/test_link.sh` -> Target: all cases pass.

- [ ] **Step 3: Run AI & MCP test suites**
  - Run: `./backend/tests/test_ai_loop_hardening.sh`
  - Run: `./backend/tests/test_mcp.sh`
  - Run: `./backend/tests/test_mcp_server.sh`

- [ ] **Step 4: Run frontend production build**
  - Run: `npm --prefix frontend run build` -> Target: 0 errors.

- [ ] **Step 5: Check off all plan tasks and commit**
  - Commit: `git commit -m "docs: 📝 mark milestone 3 implementation plan as fully verified"`
