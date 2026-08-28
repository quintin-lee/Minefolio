# AI Financial Workflow Enhancement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a multi-step AI financial workflow engine with preset pipelines (monthly review, portfolio rebalancing, expense decision) in C backend with SSE streaming, and integrate timeline step progress cards and workflow launch bar in Vue 3 Chat.

**Architecture:** 
- Backend: C23 workflow engine (`ai_workflow_service.h/.c`) with state machine, data aggregators, and SSE event streaming (`workflow_start`, `step_start`, `step_progress`, `step_complete`, `token`, `workflow_complete`).
- Frontend: TypeScript API client (`runWorkflowStream`), Pinia chat store integration, `WorkflowBar.vue` trigger panel, and `WorkflowProgressCard.vue` step timeline component in Chat.

**Tech Stack:** C23 (csilk framework, SQLite/PostgreSQL), Vue 3 (TypeScript, Pinia, Element Plus, Iconify, marked, highlight.js, mermaid).

---

### Task 1: Backend C Workflow Service & Definition Registry

**Files:**
- Create: `backend/src/services/ai_workflow_service.h`
- Create: `backend/src/services/ai_workflow_service.c`

- [ ] **Step 1: Create `backend/src/services/ai_workflow_service.h`**

Define workflow structures, step descriptors, and function signatures (`ai_workflow_get_all`, `ai_workflow_run_handler`).

- [ ] **Step 2: Implement `backend/src/services/ai_workflow_service.c`**

Implement:
- `wf_monthly_review`: Data Aggregation -> Trend/Anomaly Check -> Health Scoring -> Report Generation;
- `wf_portfolio_rebalance`: Scan Holdings -> Calculate Exposures -> PnL Attribution -> Rebalance Proposal;
- `wf_expense_decision`: Liquidity Assessment -> Safety Margin Test -> Financing Comparison -> Decision Proposal;
- SSE streaming loop pushing events `workflow_start`, `step_start`, `step_progress`, `step_complete`, `token`, `workflow_complete`.

- [ ] **Step 3: Build backend to verify compilation**

Run: `cmake --build backend/build --parallel`
Expected: 0 errors, build succeeds.

- [ ] **Step 4: Commit**

```bash
git add backend/src/services/ai_workflow_service.h backend/src/services/ai_workflow_service.c
git commit -m "feat(ai): ✨ implement financial workflow engine with SSE streaming in C"
```

---

### Task 2: Backend Controller & Route Registration

**Files:**
- Modify: `backend/src/controllers/ai_controller.h`
- Modify: `backend/src/controllers/ai_controller.c`

- [ ] **Step 1: Register `/api/ai/workflows` and `/api/ai/workflows/run` in `ai_controller.c`**

Add `workflows_list_handler` and `workflows_run_handler`, and register GET and POST routes in `register_ai_routes`.

- [ ] **Step 2: Build backend**

Run: `cmake --build backend/build --parallel`
Expected: 0 errors.

- [ ] **Step 3: Commit**

```bash
git add backend/src/controllers/ai_controller.h backend/src/controllers/ai_controller.c
git commit -m "feat(ai): ✨ expose workflow list and execution endpoints"
```

---

### Task 3: Backend Integration Tests for Workflows

**Files:**
- Modify: `backend/tests/test_link.sh`

- [ ] **Step 1: Add automated test cases for workflow endpoints**

Add Case 36 in `backend/tests/test_link.sh` checking `/api/ai/workflows` list structure and `/api/ai/workflows/run` SSE output.

- [ ] **Step 2: Run test_link.sh**

Run: `./backend/tests/test_link.sh`
Expected: PASS with 100% test success.

- [ ] **Step 3: Commit**

```bash
git add backend/tests/test_link.sh
git commit -m "test(ai): ✅ add integration tests for financial workflows"
```

---

### Task 4: Frontend Types, API, and Store Integration

**Files:**
- Modify: `frontend/src/types/index.ts`
- Modify: `frontend/src/api/ai.ts`
- Modify: `frontend/src/stores/chat.ts`

- [ ] **Step 1: Add Workflow TypeScript interfaces in `frontend/src/types/index.ts`**

Define `WorkflowDef`, `WorkflowStepDef`, `WorkflowStepState`, `WorkflowEventChunk`.

- [ ] **Step 2: Add `fetchWorkflows` and `runWorkflowStream` in `frontend/src/api/ai.ts`**

Implement SSE reader parsing `workflow_start`, `step_start`, `step_progress`, `step_complete`, `token`, `workflow_complete`.

- [ ] **Step 3: Add workflow state actions in `frontend/src/stores/chat.ts`**

Add `runWorkflow(workflowId, params)` handling active steps, streaming text, and history updates.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/types/index.ts frontend/src/api/ai.ts frontend/src/stores/chat.ts
git commit -m "feat(ai): ✨ add frontend workflow API and chat store actions"
```

---

### Task 5: Frontend UI Components: `WorkflowBar` and `WorkflowProgressCard`

**Files:**
- Create: `frontend/src/components/WorkflowBar.vue`
- Create: `frontend/src/components/WorkflowProgressCard.vue`
- Modify: `frontend/src/components/ChatMessageContent.vue`
- Modify: `frontend/src/views/Chat.vue`

- [ ] **Step 1: Create `WorkflowProgressCard.vue`**

Render timeline of steps with status badges (`pending`, `running`, `completed`, `error`), pulse animations, and expandable step summary.

- [ ] **Step 2: Create `WorkflowBar.vue`**

Render workflow cards with trigger buttons and parameter modals (e.g. month picker or amount input).

- [ ] **Step 3: Integrate into `ChatMessageContent.vue` and `Chat.vue`**

Wire `WorkflowProgressCard` inside chat messages with `workflowData`, and place `WorkflowBar` above the message input box.

- [ ] **Step 4: Build and test frontend**

Run: `npm --prefix frontend run build && npm --prefix frontend test`
Expected: 0 errors, all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/WorkflowBar.vue frontend/src/components/WorkflowProgressCard.vue frontend/src/components/ChatMessageContent.vue frontend/src/views/Chat.vue
git commit -m "feat(chat): ✨ integrate WorkflowBar and WorkflowProgressCard in Chat view"
```

---

### Task 6: Full System Verification

- [ ] **Step 1: Run full verification suite**

Run: `cmake --build backend/build --parallel && ./backend/tests/test_link.sh && npm --prefix frontend run build && npm --prefix frontend test`
Expected: 100% PASS across backend and frontend.
