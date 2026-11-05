# Milestone 4 Implementation Plan: Dynamic Conditions & Saga-Compensated Workflow Engine

- **Goal**: Implement dynamic condition branching, exponential retry, and reverse Saga rollback compensation in the C23 AI workflow runtime, enabling resilient multi-step analysis and recovery without external distributed dependencies.
- **Branch**: `feature/saga-workflow-engine`
- **Spec Reference**: [`docs/superpowers/specs/2026-11-02-ledger-and-ai-workflow-engine-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-11-02-ledger-and-ai-workflow-engine-design.md) (Section 4, Milestone 4)
- **Status**: Ready to Implement

---

## Pre-flight Conflict Scan

| Component / Task Pair | Shared Interface / File | Finding & Ruling | Status |
| :--- | :--- | :--- | :--- |
| Task 1 & Existing Workflows | `node.h`, 13 workflow definitions | New fields (`condition`, `compensate`, `max_retries`) are zero-initialized (`NULL`/`0`) in existing structs. 100% backward compatible. | Clean |
| Task 2 & Task 3 | `executor.c`, `test_workflow_branching.c` | Task 2 exposes SSE event types and execution loop; Task 3 exercises and tests them. Compatible. | Clean |
| Task 2 & SSE Client | `csilk_sse_send` | Adds `step_skipped` and `step_compensated` SSE events, preserving existing `step_start`, `step_complete`, `step_progress`, `workflow_complete`, and `error` contracts. | Clean |
| Task 4 & Financial Health | `financial_health.c` | Adds conditional debt/liability branch node; default behavior preserved if liabilities present or absent. | Clean |

---

## Tasks

### Task 1: Workflow Node Schema Extension & Type Definitions

**Files:**
- Modify: `backend/src/services/ai/workflow/node.h`
- Modify: `backend/src/services/ai/workflow/context.h`
- Modify: `backend/src/services/ai/workflow/context.c`

- [x] **Step 1: Declare callback types in `node.h`**
  - Define `ai_wf_condition_fn`:
    ```c
    typedef bool (*ai_wf_condition_fn)(csilk_db_pool_t* pool,
                                       int64_t          user_id,
                                       const csilk_json_t* shared_state);
    ```
  - Define `ai_wf_compensate_fn`:
    ```c
    typedef int (*ai_wf_compensate_fn)(csilk_db_pool_t* pool,
                                      int64_t          user_id,
                                      const csilk_json_t* shared_state);
    ```
  - Extend `ai_workflow_node_t` with:
    - `ai_wf_condition_fn   condition;`
    - `ai_wf_compensate_fn  compensate;`
    - `int                  max_retries;`

- [x] **Step 2: Verify build and backward compatibility**
  - Run `cmake --build backend/build --parallel` -> expect 0 errors.

- [x] **Step 3: Commit Task 1**
  - Commit: `git commit -m "feat(workflow): 📦 extend node schema with condition, compensation, and retry"`

---

### Task 2: Dynamic Branching, Exponential Retry & Reverse Saga Compensation Engine

**Files:**
- Modify: `backend/src/services/ai/workflow/executor.c`

- [x] **Step 1: Implement condition evaluation in `executor.c`**
  - Before invoking `node->execute`, check `if (node->condition)`:
    - If `!node->condition(pool, user_id, ctx_obj)`:
      - Emit SSE event `step_skipped` with payload `{step_index, step_id, title, reason: "condition_not_met"}`.
      - Advance loop to next step without failure.

- [x] **Step 2: Implement retry loop with backoff**
  - If `node->max_retries > 0`, wrap execution in retry loop:
    - On failure (`step_out == NULL` or `parsed_out == NULL`), if `attempt < node->max_retries`:
      - Sleep with exponential backoff (e.g. 50ms * (1 << attempt)).
      - Retry step execution.

- [x] **Step 3: Implement reverse Saga compensation stack**
  - Maintain a stack of successfully completed step indices `succeeded_steps[]`.
  - When a step fails unrecoverably:
    - Iterate in reverse order: `for (int s = success_count - 1; s >= 0; s--)`:
      - Retrieve node `const ai_workflow_node_t* prev = &graph->nodes[succeeded_steps[s]];`
      - If `prev->compensate`:
        - Invoke `prev->compensate(pool, user_id, ctx_obj)`.
        - Emit SSE event `step_compensated` with payload `{step_index, step_id, title, status: "compensated"}`.
    - Emit SSE event `workflow_failed` with payload `{workflow_id, failed_step, error}`.
    - Return -1.

- [x] **Step 4: Verify build**
  - Run `cmake --build backend/build --parallel` -> expect 0 errors.

- [x] **Step 5: Commit Task 2**
  - Commit: `git commit -m "feat(workflow): ⚡ implement dynamic branching, exponential retry, and Saga compensation"`

---

### Task 3: Unit Test Suite for Branching, Retries, and Saga Compensation

**Files:**
- Create: `backend/tests/unit/test_workflow_branching.c`
- Modify: `backend/CMakeLists.txt`

- [x] **Step 1: Write `test_workflow_branching.c`**
  - Scenario 1: Condition branching (Step 1 evaluates condition -> returns false -> step is skipped with `step_skipped` event; terminal step completes).
  - Scenario 2: Transient failure retry (Step fails twice, succeeds on attempt 3 within `max_retries=3`; workflow completes).
  - Scenario 3: Reverse Saga compensation (Step 1 and Step 2 succeed and register compensation; Step 3 fails permanently; Step 2 and Step 1 compensation callbacks invoked in exact reverse order 2 -> 1; `step_compensated` events emitted).
  - Scenario 4: Step with `compensate = NULL` in stack handled safely.
  - Scenario 5: Regression check on existing error behavior (`test_workflow_step_error` contract preserved).

- [x] **Step 2: Register test target in `backend/CMakeLists.txt`**
  - Add `test_workflow_branching` to CMakeLists.txt and CTest.

- [x] **Step 3: Run unit tests**
  - Run `./backend/build/test_workflow_branching` -> expect 100% PASS (5/5 PASS).
  - Run `ctest --test-dir backend/build --output-on-failure`.

- [x] **Step 4: Commit Task 3**
  - Commit: `git commit -m "test(workflow): 🧪 add unit tests for dynamic branching, retry, and Saga rollback"`

---

### Task 4: Production Workflow Enrichment (Conditional Step in Financial Health)

**Files:**
- Modify: `backend/src/services/ai/workflows/financial_health.c`
- Modify: `backend/src/services/ai/workflows/financial_health.h`

- [x] **Step 1: Add liability condition predicate**
  - Define `check_has_liabilities_condition(pool, user_id, shared_state)`:
    - Queries if user has liability assets (`credit_card`, `loan`, `other_liability`) or debt transactions.
    - If user has no liabilities, skip the debt-load ratio calculation step cleanly.

- [x] **Step 2: Verify workflow execution and tests**
  - Run `ctest --test-dir backend/build --output-on-failure`.
  - Run `./backend/tests/test_link.sh`.

- [x] **Step 3: Commit Task 4**
  - Commit: `git commit -m "feat(workflow): 🛡️ enrich financial health workflow with conditional liability analysis"`

---

### Task 5: Full Integration Verification Gate & Regression Suite

**Files:**
- Test: Full CTest unit test suite (51 suites)
- Test: `backend/tests/test_link.sh` (184 assertions)
- Test: `backend/tests/test_ai_loop_hardening.sh`
- Test: `backend/tests/test_mcp.sh` & `backend/tests/test_mcp_server.sh`
- Build: Frontend production build
- Modify: `docs/superpowers/plans/2026-11-05-saga-workflow-engine.md`

- [x] **Step 1: Run complete CTest unit test suites**
  - Target: 51/51 test suites passed (100%).

- [x] **Step 2: Run `test_link.sh`**
  - Target: 184/184 assertions passed (100%).

- [x] **Step 3: Run AI & MCP test suites**
  - Target: 100% pass (28/28, 14/14, 10/10 PASS).

- [x] **Step 4: Run frontend production build**
  - Target: 0 errors.

- [x] **Step 5: Check off all plan tasks and commit**
  - Commit: `git commit -m "docs: 📝 mark milestone 4 implementation plan as fully verified"`
