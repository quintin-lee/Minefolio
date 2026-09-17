# MCP Hardening Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the six verified gaps in the MCP subsystem: (1) stop the stdio pool from serializing concurrent dispatch threads on `initialize` I/O, (2) make the policy engine honor the cached MCP `risk_level` instead of clobbering it with a name-prefix re-assessment, (3) add schema validation to Path B `/mcp` `tools/call`, (4) rate-limit the `/mcp` endpoint per user, (5) fill the audit `result_summary` for MCP calls, (6) collapse the N per-server tool-count queries in the list endpoint into one.

**Architecture:** Each task is a focused, independently testable change to an existing file (no new modules). The stdio-pool task splits the lock scope so spawn/handshake I/O happens outside `s_pool_lock`. The risk task threads the cached risk into `ai_policy_evaluate` via an optional override parameter so the MCP bridge's cache-derived risk wins over the name-pattern fallback. The remaining tasks are small, surgical edits with existing test seams.

**Tech Stack:** C23, csilk HTTP/JSON/DB framework, pthreads, libcurl, SQLite, CTest unit tests (`backend/tests/unit/`), bash integration tests (`backend/tests/test_mcp*.sh`).

## Ground Rules for the Implementer (read first)

- Build MUST use Makefiles: `cmake -B backend/build -G "Unix Makefiles"`. Never Ninja (stale-dependency bug).
- Build: `cmake --build backend/build --parallel`. Tests: `cd backend/build && ctest --output-on-failure`.
- All SQL uses `?` placeholders via `csilk_db_query_param_json`; never interpolate user input.
- Response envelope is always `{code, message, data}` on HTTP 200 via `respond_ok`/`respond_error` from `common/response.h` — EXCEPT the `/mcp` JSON-RPC endpoint, which returns raw JSON-RPC frames.
- Every C function touching a shared static MUST hold the matching `pthread_mutex` for the read-modify-write; the tasks below spell this out.
- After each task: build clean (zero warnings), run the targeted test, then the full CTest suite. Commit per task.
- Reference skills: @verification-before-completion (run the test before claiming pass), @commit (convention: `fix(mcp): 🎯 subject`).

## File Map

| File | Responsibility in this plan |
|---|---|
| `backend/src/services/ai/tools/mcp/mcp_stdio_pool.c` | Task 1: split lock scope around `initialize` |
| `backend/src/services/ai/policy/policy.h` / `policy.c` | Task 2: optional `risk_override` param on `ai_policy_evaluate` |
| `backend/src/services/ai/policy/risk.h` | Task 2: add `AI_RISK_NO_OVERRIDE` sentinel to the `ai_risk_level_t` enum |
| `backend/src/services/ai/tools/mcp/mcp_bridge.c` | Task 2 (call site) + Task 5 (audit `result_summary`) |
| `backend/src/interfaces/http/controllers/mcp_server_controller.c` | Task 3: schema-validate Path B `tools/call` |
| `backend/src/main.c` + `backend/src/middlewares/rate_limit.{h,c}` | Task 4: per-user rate limit on `/mcp` |
| `backend/src/application/mcp/usecases.c` + `domain/mcp/repository.h` + `infrastructure/repositories/mcp_server_repo_impl.{c,h}` | Task 6: one-query tool-count map |
| `backend/tests/unit/test_mcp_pool.c` (new) | Task 1 regression test |
| `backend/tests/unit/test_policy_mcp_risk.c` (new) | Task 2 regression test |
| `backend/CMakeLists.txt` | Wire both new tests (mirror `test_ai_policy` / `test_domain_mcp` blocks) |
| `backend/tests/test_mcp_server.sh` (existing) | Task 3/4 integration regression |

## Correctness Notes (verified against current code)

These are things that are ALREADY correct and you must NOT "fix" or you will break working behavior:
- `mf_mcp_client_free` **already** reaps the stdio child (SIGTERM → poll → SIGKILL → blocking `waitpid`). Do not add a second reaper.
- `curl_header_cb` **already** captures `Mcp-Session-Id` into `c->session_id`, and `http_call` **already** sends it back on subsequent frames. Do not add session persistence.
- `mf_mcp_stdio_pool_acquire` **already** caps the pool at `MINEFOLIO_MCP_STDIO_MAX_PROCS` (line ~65). Do not re-add that cap.

---

## Chunk 1: Runtime / Safety (Tasks 1–4)

### Task 1: Decouple stdio-pool `initialize` I/O from `s_pool_lock`

**Why:** `mf_mcp_stdio_pool_acquire` (mcp_stdio_pool.c:37-122) acquires `s_pool_lock` at line 52 and only releases it at lines 60/87/100/108/120. Inside that critical section it calls `mf_mcp_client_new` + `mf_mcp_client_initialize` (lines 98-110, and the pool-full fallback at 88-92). For a stdio server, `initialize` spawns a child process and performs an MCP handshake — seconds of I/O — while holding the pool mutex. Every other worker thread doing an MCP stdio dispatch blocks on that lock until the handshake finishes. Result: all stdio MCP calls serialize on one handshake at a time, even for *different* (user, server) pairs.

**Fix:** Do slot bookkeeping under the lock, but perform `initialize` **outside** the lock. Reserve the slot first (create the client, mark the slot in_use but not yet ready), release the lock, then `initialize`; on failure, take the lock, mark the slot ready=false, release, and free the client. A concurrent `acquire` that hits an in-progress slot must NOT hand it out before it is ready — instead it takes the fallback path (its own short-lived client) rather than deadlocking on an unfinished slot.

**Files:**
- Modify: `backend/src/services/ai/tools/mcp/mcp_stdio_pool.c:11-122`
- Test: `backend/tests/unit/test_mcp_pool.c` (new — create if absent; see Step 1)

- [ ] **Step 1: Write the failing regression test**

Create `backend/tests/unit/test_mcp_pool.c` if it does not exist (check with a `glob` first). It needs a way to observe that two concurrent `acquire` calls for *different* (user, server) pairs do not serialize on the lock. Because we cannot easily force a slow handshake in a unit test without a real stdio server, test the *contract* instead: `acquire` on a pool that is disabled (max_procs=0) returns a working client (short-lived path) and `release` on it does NOT crash — proving the short-lived fallback is safe to use from multiple threads. This is the minimal invariant that guards the refactor (the fallback path is exactly what a concurrent acquire will now take while another thread's handshake is in flight).

```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "services/ai/tools/mcp/mcp_stdio_pool.h"
#include "services/ai/tools/mcp/mcp_config.h"
#include "domain/mcp/entity.h"

int main(void)
{
    /* Force the pool to be disabled so acquire takes the short-lived path. */
    setenv("MINEFOLIO_MCP_STDIO_MAX_PROCS", "0", 1);
    assert(mf_mcp_config_stdio_max_procs() == 0);

    mf_mcp_server_t srv = {0};
    srv.user_id    = 1;
    srv.id         = 99;
    srv.transport  = MCP_TRANSPORT_STDIO;
    strcpy(srv.name, "probe");
    srv.command[0] = '\0'; /* no real spawn — we only exercise pool bookkeeping */

    char err[256];
    /* Each acquire here either yields a short-lived client or NULL+err (spawn
     * fails because command is empty). Neither must corrupt the pool. */
    for (int i = 0; i < 4; i++) {
        mf_mcp_client_t* c = mf_mcp_stdio_pool_acquire(1, 99, &srv, err, sizeof(err));
        if (c) {
            mf_mcp_stdio_pool_release(c); /* short-lived: release == free */
        }
        /* On failure err is set; that is an acceptable outcome, not a crash. */
    }
    int shutdowns = mf_mcp_stdio_pool_shutdown();
    printf("PASS: test_mcp_pool (shutdowns=%d)\n", shutdowns);
    (void)shutdowns;
    return 0;
}
```

Note: an empty `command` makes `mf_mcp_client_initialize` fail the spawn, so `acquire` returns NULL with `err` set. The test asserts *no crash / no pool corruption*, not a successful handshake — that is the honest, deterministic invariant for a unit test. If `mf_mcp_client_new`/`initialize` cannot tolerate an empty command without a segfault, replace the loop body with a single `acquire` on a *valid* command (`"true"` — the shell builtin path) and assert the returned client is non-NULL then `release` it; adjust after the first run.

- [ ] **Step 2: Wire the test into CMake and run the pre-refactor baseline**

All unit-test wiring lives in `backend/CMakeLists.txt` (there is NO `tests/unit/CMakeLists.txt` — see `test_domain_mcp` at line 236-240 and `test_ai_policy` at line 169-173). The pool test needs the full library link (not just a domain rules file), so mirror the `test_ai_policy` pattern:
```cmake
add_executable(test_mcp_pool tests/unit/test_mcp_pool.c ${LIB_SOURCES})
target_include_directories(test_mcp_pool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_mcp_pool PRIVATE -UNDEBUG)
target_link_libraries(test_mcp_pool PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES} m)
add_test(NAME test_mcp_pool COMMAND test_mcp_pool)
```
Place it near the `test_domain_mcp` block. Then build and run:
```bash
cmake -B backend/build -G "Unix Makefiles" && cmake --build backend/build --parallel
cd backend/build && ctest -R test_mcp_pool --output-on-failure
```
Expected: PASS. This is the green baseline *before* the lock refactor, so the refactor cannot silently regress the short-lived fallback path.

- [ ] **Step 3: Refactor the lock scope**

In `mf_mcp_stdio_pool_acquire`, restructure the "new entry" and "pool-full fallback" paths so `mf_mcp_client_new` + `mf_mcp_client_initialize` run *after* `pthread_mutex_unlock`. Concretely:

1. Keep `find_idle_slot` and the ready-slot hand-out (lines 54-62) under the lock — those are pure bookkeeping.
2. For the "new entry" path (currently lines 97-120): under the lock, decide *whether* a slot will be added and pre-reserve the slot index (set `in_use=true`, `client=NULL` sentinel meaning "in progress", bump `s_pool_count`). Unlock. Then build + initialize the client. Re-lock, write the client into the reserved slot, unlock.
3. For a concurrent `acquire` that sees a `client==NULL` "in-progress" slot for the *same* (user, server): unlock and take the short-lived fallback (lines 87-93) — do NOT wait on the in-progress handshake.
4. For the pool-full eviction path (lines 70-94): eviction frees a ready slot under the lock (that `mf_mcp_client_free` is already lock-safe because it only touches the child, not the pool), then the new-entry construction happens outside the lock as in (2).

The reserved-but-in-progress sentinel is the key invariant: a slot is either "ready" (client non-NULL) or "in progress" (client NULL, in_use true). `release`, `evict_idle`, and `shutdown` MUST treat a `client==NULL` slot as a no-op for the child (`mf_mcp_client_free` is a no-op on NULL, mcp_client.c:667-699) but still compact it out, so the slot is reclaimed on the next pass.

- [ ] **Step 4: Run the regression test + full CTest**

Run:
```bash
cd backend/build && ctest -R test_mcp_pool --output-on-failure
cd backend/build && ctest --output-on-failure
```
Expected: `test_mcp_pool` PASS and the full suite green. If any suite regresses, the refactor corrupted bookkeeping — fix before proceeding.

- [ ] **Step 5: Commit**

```bash
git add backend/src/services/ai/tools/mcp/mcp_stdio_pool.c backend/tests/unit/test_mcp_pool.c backend/CMakeLists.txt
git commit -m "fix(mcp): 🎯 run stdio pool handshake outside s_pool_lock"
```

---

### Task 2: Honor cached MCP `risk_level` in the policy engine

**Why:** `mcp_bridge_dispatch` (mcp_bridge.c:474-657) correctly derives `risk` from the tool cache (line 531, `mcp_risk_string_to_level`) and escalates mutation tools to at least MEDIUM (line 533). It then calls `ai_policy_evaluate(ctx->user_id, ctx->session_id, tool_name, args)` (line 554). But inside `ai_policy_evaluate` (policy.c:126) the decision's `risk_level` is set by `ai_risk_assess(tool_name, args)` — which keys off the *tool name prefix* (`mcp:7:get_foo` starts with `mcp:` → falls to the default `AI_RISK_LOW` in risk.c:96; it never inspects the cached remote risk). So a remote tool the operator marked `risk_level=high` gets re-flattened to LOW by the policy engine, and the confirmation gate at policy.c:191 (`risk_level >= AI_RISK_MEDIUM`) never fires. The bridge's careful cache-derived risk is discarded.

**Fix:** Give `ai_policy_evaluate` an optional risk override. When the caller (the MCP bridge) has an authoritative risk from the tool cache, pass it in and let it win over the name-pattern fallback. Existing call sites pass "no override" and keep today's behavior.

**Files:**
- Modify: `backend/src/services/ai/policy/policy.h:60-63` (signature) and `policy.c:113-196` (body)
- Modify: `backend/src/services/ai/policy/risk.h:14-20` — add an explicit `AI_RISK_NO_OVERRIDE` sentinel to the `ai_risk_level_t` enum
- Modify: `backend/src/services/ai/tools/mcp/mcp_bridge.c:554` (call site)
- Migrate call sites: `backend/src/services/ai/tools/dispatcher.c` (builtin tools), `backend/tests/unit/test_ai_policy.c` (4 call sites at lines 288, 294, 323, 334 — MUST all get the 5th arg or the build breaks)
- Test: `backend/tests/unit/test_policy_mcp_risk.c` (new)

- [ ] **Step 1: Write the failing test**

Wire the test into `backend/CMakeLists.txt` mirroring the `test_ai_policy` pattern (lines 169-173) — it links the full lib:
```cmake
add_executable(test_policy_mcp_risk tests/unit/test_policy_mcp_risk.c ${LIB_SOURCES})
target_include_directories(test_policy_mcp_risk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(test_policy_mcp_risk PRIVATE -UNDEBUG)
target_link_libraries(test_policy_mcp_risk PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES})
add_test(NAME test_policy_mcp_risk COMMAND test_policy_mcp_risk)
```
Then add `backend/tests/unit/test_policy_mcp_risk.c`. Pin the rules with `ai_policy_set_rules` + `ai_policy_reset_frequency_limits` (exactly as `test_ai_policy.c` Test 10/11 do at lines 273-280 / 309-316) so `requires_confirmation` is deterministic and not dependent on whatever global rules the process happens to carry:

```c
#include <assert.h>
#include <stdio.h>
#include "services/ai/policy/policy.h"
#include "services/ai/policy/risk.h"

int main(void)
{
    /* Pin deterministic rules: confirmation enforced for medium+, no freq cap in the way. */
    ai_policy_rules_t rules = {
        .single_amount_limit = 500000.0,
        .large_amount_threshold = 50000.0,
        .max_frequency_per_minute = 60,
        .enforce_confirmation = true,
    };
    ai_policy_set_rules(&rules);
    ai_policy_reset_frequency_limits();

    /* A real user id so auth passes; tool name looks like a read tool by prefix
     * so the name-pattern fallback would compute a low/readonly risk. */
    const char* tool = "mcp:7:get_foo";

    /* No override → today's name-pattern behavior (low-ish, not CRITICAL). */
    ai_policy_decision_t* d1 = ai_policy_evaluate(1, 0, tool, NULL, AI_RISK_NO_OVERRIDE);
    assert(d1);
    assert(d1->risk_level < AI_RISK_CRITICAL);
    ai_policy_decision_free(d1);

    /* Override with HIGH → the decision must carry HIGH and require confirmation
     * (enforce_confirmation is pinned true above). */
    ai_policy_decision_t* d2 = ai_policy_evaluate(1, 0, tool, NULL, AI_RISK_HIGH);
    assert(d2);
    assert(d2->risk_level == AI_RISK_HIGH);
    assert(d2->requires_confirmation == true);
    ai_policy_decision_free(d2);

    printf("PASS: test_policy_mcp_risk\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails to compile (new param + sentinel don't exist yet)**

Run: `cd backend/build && ctest -R test_policy_mcp_risk --output-on-failure`
Expected: FAIL — `AI_RISK_NO_OVERRIDE` undefined and `ai_policy_evaluate` arity mismatch. This confirms the test targets the change.

- [ ] **Step 3: Add the sentinel and override parameter**

In `risk.h`, `ai_risk_level_t` is a C enum (values 0-4, READ_ONLY through CRITICAL). Add one distinct sentinel constant that is NOT a real level — use `AI_RISK_NO_OVERRIDE = -1` (sentinels that are real level values would be indistinguishable from a genuine "override to LOW"). Leave all existing level values unchanged:
```c
typedef enum {
    AI_RISK_READ_ONLY = 0,
    AI_RISK_LOW = 1,
    AI_RISK_MEDIUM = 2,
    AI_RISK_HIGH = 3,
    AI_RISK_CRITICAL = 4,
    AI_RISK_NO_OVERRIDE = -1  /* sentinel: "no override, use name-pattern fallback" */
} ai_risk_level_t;
```
Note `ai_risk_level_from_string` (risk.c:25-46) does a `switch`-less string compare and falls through to `AI_RISK_LOW` for unknown — the sentinel is only used as a *pass-by-value override arg*, never fed to `from_string`, so no string mapping is needed.

In `policy.h`, change the signature to:
```c
ai_policy_decision_t* ai_policy_evaluate(int64_t             user_id,
                                         int64_t             session_id,
                                         const char*         tool_name,
                                         const csilk_json_t* args,
                                         ai_risk_level_t     risk_override);
```

In `policy.c`, after computing the name-pattern risk (line 126), apply the override:
```c
dec->risk_level = ai_risk_assess(tool_name, args);
if (risk_override != AI_RISK_NO_OVERRIDE) {
    dec->risk_level = risk_override;
}
```
The confirmation check at line 191 already keys off `dec->risk_level`, so the override flows through it with no other change.

**Migrate every existing call site** of `ai_policy_evaluate` to pass `AI_RISK_NO_OVERRIDE` so behavior is unchanged. The complete list (verified by grep):
```bash
grep -rn "ai_policy_evaluate" backend/src backend/tests
```
Required migrations:
- `backend/src/services/ai/tools/dispatcher.c` — the builtin tool dispatcher (one call site). Pass `AI_RISK_NO_OVERRIDE`.
- `backend/tests/unit/test_ai_policy.c` — lines 288, 294, 323, 334 (four call sites). **All four MUST get the 5th argument `AI_RISK_NO_OVERRIDE` or the build fails** with "too few arguments to function".
- `backend/src/services/ai/tools/mcp/mcp_bridge.c` (~line 554) — this is the ONLY site that passes a real override (see Step 4); it is NOT migrated to the sentinel.
If grep surfaces any other caller, add `AI_RISK_NO_OVERRIDE` to it too.

- [ ] **Step 4: Update the MCP bridge call site to pass its cache-derived risk**

In `mcp_bridge.c` at the `ai_policy_evaluate` call (~line 554), pass `risk` (the cache-derived, mutation-escalated value already computed at lines 531-534) instead of `AI_RISK_NO_OVERRIDE`:
```c
ai_policy_decision_t* decision =
    ai_policy_evaluate(ctx->user_id, ctx->session_id, tool_name, args, risk);
```
If `risk` ended up `AI_RISK_NO_OVERRIDE`-like for a tool with no cached row (the anonymous fallback at line 524 sets risk via `mcp_risk_string_to_level("")` → LOW), that is fine — LOW still feeds the confirmation gate correctly.

- [ ] **Step 5: Run the test + full CTest**

Run:
```bash
cd backend/build && ctest -R test_policy_mcp_risk --output-on-failure
cd backend/build && ctest --output-on-failure
```
Expected: new test PASS; full suite green (proves no caller regressed).

- [ ] **Step 6: Commit**

```bash
git add backend/src/services/ai/policy/risk.h backend/src/services/ai/policy/policy.h backend/src/services/ai/policy/policy.c backend/src/services/ai/tools/mcp/mcp_bridge.c backend/src/services/ai/tools/dispatcher.c backend/tests/unit/test_ai_policy.c backend/tests/unit/test_policy_mcp_risk.c backend/CMakeLists.txt
git commit -m "fix(mcp): 🎯 let cached MCP risk override the name-pattern risk in policy"
```

---

### Task 3: Schema-validate Path B `/mcp` `tools/call`

**Why:** `mcp_handle_tools_call` (mcp_server_controller.c:116-152) calls `ai_tools_execute_parsed(pool, user_id, 0, args_json, name)` directly, bypassing the dispatcher's schema validation step. An external MCP client sending malformed `arguments` hits the builtin tool executor with unvalidated input. The builtin path (dispatcher.c step 3, `ai_tool_validate_args`) rejects bad args with a clean error before execution; the Path B endpoint must do the same.

**Fix:** Resolve the tool by name, run `ai_tool_validate_args` on `args_json` against the tool's `parameters_schema`, and on failure return JSON-RPC error `-32602` (invalid params) instead of executing.

**Files:**
- Modify: `backend/src/interfaces/http/controllers/mcp_server_controller.c:116-152`
- Test: extend `backend/tests/test_mcp_server.sh` with an invalid-params case

- [ ] **Step 1: Add a failing integration case to `test_mcp_server.sh`**

Find the existing `tools/call` block in `test_mcp_server.sh` (it greps for `"content":\[` and `"type":"text"`). Add, right after it, a call with deliberately wrong argument shape for a tool that has a required parameter (pick one from the `tools/list` output the test already asserts, e.g. `get_assets` if it has a required param; otherwise a tool the test already lists). Assert the response contains `-32602`:

```bash
BAD=$(curl -s -X POST "http://127.0.0.1:$PORT/mcp" \
    -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"<TOOL>","arguments":{}}}' || true)
grep_check "mcp: tools/call rejects invalid params with -32602" '"code":-32602' "$BAD"
```
Choose `<TOOL>` by reading `test_mcp_server.sh`'s `tools/list` assertions so the tool definitely has a schema that rejects an empty object. The goal is a deterministic rejection.

- [ ] **Step 2: Run to verify it fails**

Run: `bash backend/tests/test_mcp_server.sh`
Expected: the new `grep_check` FAILS (current code executes the tool and returns `content`, not `-32602`).

- [ ] **Step 3: Implement schema validation in the handler**

In `mcp_handle_tools_call` (mcp_server_controller.c:116-152), after resolving `name` and copying `args_json`, look up the tool and validate. The file already includes `"services/ai/tools/registry.h"` (line 7) — the only NEW include is `validation.h`:
```c
#include "services/ai/tools/validation.h"

const ai_tool_t* tool = ai_tool_find(name);
if (!tool) {
    /* Unknown tool: surface as -32602 so external clients get a clean code. */
    csilk_json_free(args_json);
    csilk_json_free(params);
    mcp_send_response(c, mcp_req_id(body), NULL, -32602, "unknown tool");
    return;
}
char verr[256] = {0};
if (ai_tool_validate_args(tool->parameters_schema, args_json, verr, sizeof(verr)) != 0) {
    csilk_json_free(args_json);
    csilk_json_free(params);
    mcp_send_response(c, mcp_req_id(body), NULL, -32602, verr);
    return;
}
```
Read `validation.h` first to confirm `ai_tool_validate_args`'s exact signature (arg order, return value, buffer-size param) and that `ai_tool_t.parameters_schema` is the correct field name (the dispatcher at dispatcher.c:80-107 uses `tool->parameters_schema`). Confirm `ai_tool_find` is exported from `registry.h` (dispatcher.c:56 uses it). Confirm `mcp_send_response`'s exact parameter list so the calls compile (it is defined in the same file, lines 40-65).
- [ ] **Step 4: Run the integration test + full suite**

Run:
```bash
bash backend/tests/test_mcp_server.sh
cd backend/build && ctest --output-on-failure
```
Expected: new `-32602` case PASS; existing `tools/call` cases still PASS (valid calls unaffected); full CTest green.

- [ ] **Step 5: Commit**

```bash
git add backend/src/interfaces/http/controllers/mcp_server_controller.c backend/tests/test_mcp_server.sh
git commit -m "fix(mcp): 🎯 schema-validate Path B tools/call arguments"
```

---

### Task 4: Per-user rate limit on `/mcp`

**Why:** `rate_limit_auth_middleware` (rate_limit.c:85-148) only guards four auth paths (`/api/auth/login`, `/api/auth/register`, `/api/system/setup`, `/api/auth/2fa/verify-login`). The `/mcp` group (main.c:107) runs JWT but has **no** request cap. A valid JWT can hammer `POST /mcp` → `tools/call` → builtin mutation tools unboundedly. We need a per-user (not per-IP) cap on `/mcp`, keyed by `user_id` from the JWT.

**Fix:** Add a dedicated middleware `mcp_rate_limit_middleware` with its own ring buffer keyed on `user_id`, and register it on the `/mcp` group in `main.c` *after* the JWT middleware (so `ctx_user_id(c)` is populated).

**Files:**
- Modify: `backend/src/middlewares/rate_limit.h` (declare) and `rate_limit.c` (implement)
- Modify: `backend/src/main.c:104-110` (register on `/mcp` group)
- Test: extend `backend/tests/test_mcp_server.sh` with a burst case

- [ ] **Step 1: Add a failing integration case**

In `test_mcp_server.sh`, after the existing `/mcp` cases, add a burst that exceeds the per-user cap and assert a JSON-RPC / HTTP 429-equivalent rejection. Because `/mcp` returns raw JSON-RPC, the cap should respond with a JSON-RPC error carrying a 429-ish code and a `Retry-After`-style message. Define the cap at, say, 120 req/min/user (loose enough for the test's own calls, tight enough that a second burst of 200 trips it). Add:

```bash
# Burst: fire 130 pings, expect at least one to be rate-limited.
LIMITED=0
for i in $(seq 1 130); do
  R=$(curl -s -X POST "http://127.0.0.1:$PORT/mcp" \
      -H "Authorization: Bearer $JWT" -H 'Content-Type: application/json' \
      -d '{"jsonrpc":"2.0","id":1,"method":"ping","params":{}}' || true)
  case "$R" in
    *"-32005"*|*"rate"*|*"429"*) LIMITED=1 ;;
  esac
done
grep_check "mcp: /mcp rate-limits a burst" "1" "$(echo "$LIMITED")"
```
The exact rejection signature is defined in Step 3 (a JSON-RPC error code `-32005` with a "rate limited" message). If you prefer HTTP 429 instead of a JSON-RPC error, change this case to assert on `curl -o /dev/null -w '%{http_code}'` returning 429 and implement accordingly — but pick ONE and match the test to it.

- [ ] **Step 2: Run to verify it fails**

Run: `bash backend/tests/test_mcp_server.sh`
Expected: the new burst case FAILS (no cap exists yet, all 130 pings succeed, `LIMITED` stays 0).

- [ ] **Step 3: Implement `mcp_rate_limit_middleware`**

In `rate_limit.c`, mirror the existing ring-buffer pattern but key on `user_id` (from `ctx_user_id(c)`) instead of `(ip, path)`. **The existing `entry_t` (rate_limit.c:42-46) has `char path[64]` + `char ip[64]` fields and CANNOT be reused** — define a NEW struct for the MCP ring:
```c
typedef struct {
    int64_t user_id;
    time_t  ts;
} mcp_entry_t;
```
Use a new static ring + mutex (`g_mcp_rate_mutex`) distinct from the auth ring, plus `MCP_RATE_MAX_PER_MIN` (120). On cap breach, emit a JSON-RPC error frame (not the envelope, since `/mcp` is raw JSON-RPC):
```c
csilk_json_t* resp = csilk_json_object();
csilk_json_add_string(resp, "jsonrpc", "2.0");
csilk_json_add_number(resp, "id", 0);
csilk_json_t* er = csilk_json_object();
csilk_json_add_number(er, "code", -32005);
csilk_json_add_string(er, "message", "rate limited: too many MCP requests");
csilk_json_add_object(resp, "error", er);
csilk_set_header(c, "Retry-After", "60");
csilk_json(c, CSILK_STATUS_TOO_MANY_REQUESTS, resp);
csilk_abort(c);
```
Declare it in `rate_limit.h`. Register in `main.c` after the `/mcp` JWT group:
```c
csilk_app_use_group(app, "/mcp", mcp_rate_limit_middleware);
```
Confirm `csilk_get_param`/`ctx_user_id` and `csilk_get_path` usage matches the existing middleware idioms in `rate_limit.c` before writing (the auth middleware uses `csilk_get_path(c)`; you'll use `ctx_user_id(c)`).

- [ ] **Step 4: Run the integration test + full suite**

Run:
```bash
bash backend/tests/test_mcp_server.sh
cd backend/build && ctest --output-on-failure
```
Expected: burst case PASS (some pings limited); earlier `/mcp` cases still PASS (cap is loose enough at 120/min); full CTest green.

- [ ] **Step 5: Commit**

```bash
git add backend/src/middlewares/rate_limit.h backend/src/middlewares/rate_limit.c backend/src/main.c backend/tests/test_mcp_server.sh
git commit -m "fix(mcp): 🎯 per-user rate limit on /mcp endpoint"
```

---

## Chunk 2: Audit / Data (Tasks 5–6)

### Task 5: Fill `result_summary` in the MCP audit record

**Why:** The builtin dispatcher (dispatcher.c:193-216) fills `audit.result_summary` with "Operation succeeded"/"Operation failed". The MCP bridge (mcp_bridge.c:634-645) builds an `ai_audit_record_t` but leaves `result_summary` empty, so MCP call audits are not searchable by outcome in `ai_audit_log`.

**Fix:** Mirror the dispatcher's summary fill in the bridge, using `call_failed` + `is_err`.

**Files:**
- Modify: `backend/src/services/ai/tools/mcp/mcp_bridge.c:634-645`

- [ ] **Step 1: Confirm the field exists and its type**

Read `services/ai/policy/audit.h` to confirm `ai_audit_record_t.result_summary` exists and its size (the dispatcher uses `sizeof(audit.result_summary)` with `snprintf`). The bridge must not write past it.

- [ ] **Step 2: Fill the summary (and fix the latent `is_err` bug)**

In `mcp_bridge_dispatch`, the existing line `bool is_err = (result && strstr(result, "\"error\":") != NULL);` is itself a bug: by C operator precedence this is `result && (strstr(...) != NULL)`, which yields a `char*` coerced to `bool` — it is true whenever `result` is non-NULL, even when the error marker is absent. Rewrite it as a clean guarded bool and then fill the summary:
```c
bool is_err = (result != NULL) && (strstr(result, "\"error\":") != NULL);
...
snprintf(audit.result_summary,
         sizeof(audit.result_summary),
         (is_err || call_failed) ? "MCP tool call failed" : "MCP tool call succeeded");
```
(Keep `is_err`/`call_failed` in scope where `ai_audit_log(&audit)` is called. Parenthesize the ternary condition so `is_err || call_failed` is unambiguous.)
(Match the dispatcher's terse wording style; keep it under `sizeof(audit.result_summary)`.)

- [ ] **Step 3: Build + full CTest**

Run:
```bash
cmake --build backend/build --parallel
cd backend/build && ctest --output-on-failure
```
Expected: clean build, full suite green. There is no dedicated audit-content assertion in the current suite; this is a correctness fill, verified by build + suite (and the integration test `test_mcp.sh` asserts a tool span is recorded — re-run it: `bash backend/tests/test_mcp.sh`).

- [ ] **Step 4: Commit**

```bash
git add backend/src/services/ai/tools/mcp/mcp_bridge.c
git commit -m "fix(mcp): 🎯 populate result_summary on MCP audit records"
```

---

### Task 6: Collapse N per-server tool-count queries into one

**Why:** `mf_mcp_usecase_list` (usecases.c:100-116) calls `server_to_json` per server, and `server_to_json` (usecases.c:66-98) loads that server's *entire* tool list just to count it (`mf_mcp_server_tool_repo_load` then `tool_count`). For N servers that's N full tool-list loads (each row up to 16KB schema) over the network/DB, when all the list endpoint needs is a `tool_count` integer.

**Fix:** Add a single `GROUP BY server_id` count query to the repository; `mf_mcp_usecase_list` builds one `server_id -> count` map up front and `server_to_json` reads from it instead of loading full tool rows.

**Files:**
- Modify: `backend/src/domain/mcp/repository.h` (declare `mf_mcp_server_tool_repo_counts_for_user`)
- Modify: `backend/src/infrastructure/repositories/mcp_server_repo_impl.c` + `.h` (implement the count query)
- Modify: `backend/src/application/mcp/usecases.c:66-116` (build the map once, pass counts to `server_to_json`)
- Test: existing `test_mcp.sh` / `test_mcp_server.sh` exercise the list endpoint; add a light assertion that `tool_count` is present and is a number

- [ ] **Step 1: Add the count query to the repository**

In `mcp_server_repo_impl.h`, declare:
```c
/* Returns a heap array of {server_id, count} pairs; caller frees with
 * mf_mcp_server_tool_counts_free. out_count = number of pairs. 0 on success,
 * -1 on DB error. count==0 servers are omitted (caller treats missing as 0). */
typedef struct {
    int64_t server_id;
    size_t  count;
} mf_mcp_tool_count_t;

int mf_mcp_server_tool_repo_counts_for_user(void*                  db_pool,
                                             int64_t               user_id,
                                             mf_mcp_tool_count_t** out_pairs,
                                             size_t*               out_count);
void mf_mcp_tool_counts_free(mf_mcp_tool_count_t* pairs, size_t count);
```
In `mcp_server_repo_impl.c`, implement with a single SQL (placeholder + `csilk_db_query_param_json`):
```c
int
mf_mcp_server_tool_repo_counts_for_user(void* db_pool, int64_t user_id,
                                        mf_mcp_tool_count_t** out_pairs, size_t* out_count)
{
    /* GROUP BY collapses all per-server counts into one round-trip. */
    char* sql = "SELECT server_id, COUNT(*) FROM mcp_server_tool "
                "WHERE user_id = ? GROUP BY server_id";
    /* Execute via csilk_db_query_param_json / a raw param query; accumulate
     * server_id + COUNT into a heap-grown array. Return 0 / -1 per repo contract. */
}
```
Confirm the exact csilk query helper to use by reading how `mf_mcp_server_tool_repo_load` is implemented in the same file (it uses `csilk_db_query_param_json` or `csilk_db_exec_param`; match the idiom). The SELECT returns two scalar columns; read them with the same JSON/param accessor the file already uses.

- [ ] **Step 2: Build the count map in the list usecase**

In `usecases.c`, change `mf_mcp_usecase_list` to fetch the count map once and look up each server's count instead of calling the full `server_to_json` load:
```c
int
mf_mcp_usecase_list(void* pool, int64_t user_id, csilk_json_t** out_list)
{
    mf_mcp_server_t* servers = NULL;
    size_t           count = 0;
    if (mf_mcp_server_repo_list(pool, user_id, &servers, &count) != 0) {
        return -1;
    }
    mf_mcp_tool_count_t* pairs = NULL;
    size_t               pairs_count = 0;
    mf_mcp_server_tool_repo_counts_for_user(pool, user_id, &pairs, &pairs_count);

    csilk_json_t* arr = csilk_json_array();
    for (size_t i = 0; i < count; i++) {
        size_t tc = 0;
        for (size_t p = 0; p < pairs_count; p++) {
            if (pairs[p].server_id == servers[i].id) { tc = pairs[p].count; break; }
        }
        csilk_json_add_item(arr, server_to_json(&servers[i], pool, user_id, tc));
    }
    mf_mcp_tool_counts_free(pairs, pairs_count);
    mf_mcp_server_repo_free_list(servers, count);
    *out_list = arr;
    return 0;
}
```
Change `server_to_json`'s signature to take `size_t tool_count` (drop the internal `mf_mcp_server_tool_repo_load`), and keep the `tool_count` field in the JSON (line 92) fed from the argument. Update `mf_mcp_usecase_get` (usecases.c:118-132) to also compute the single count via the count query (or a `SELECT COUNT(*)` for one server) before calling `server_to_json`.

- [ ] **Step 3: Add a list-endpoint assertion**

In `backend/tests/test_mcp.sh` (which already creates a stdio server + refreshes tools), add:
```bash
LIST=$(curl -s -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" "$BASE/ai/mcp/servers")
grep_check "mcp list returns tool_count field" '"tool_count"' "$LIST"
```
(Check `test_mcp.sh` for its `grep_check`/`TOKEN`/`BASE` conventions before writing the exact lines — they differ per script.)

- [ ] **Step 4: Build + full CTest + integration**

Run:
```bash
cmake --build backend/build --parallel
cd backend/build && ctest --output-on-failure
bash backend/tests/test_mcp.sh
```
Expected: clean build (no unused-symbol warnings from the now-removed per-server load), full suite green, `tool_count` assertion PASS.

- [ ] **Step 5: Commit**

```bash
git add backend/src/domain/mcp/repository.h backend/src/infrastructure/repositories/mcp_server_repo_impl.c backend/src/infrastructure/repositories/mcp_server_repo_impl.h backend/src/application/mcp/usecases.c backend/tests/test_mcp.sh
git commit -m "perf(mcp): 🎯 one GROUP BY query for list tool-counts instead of N loads"
```

---

## Execution Handoff

All six tasks are in one plan because they are a single concern (MCP hardening) with shared test seams (`test_mcp.sh`, `test_mcp_server.sh`, the policy/audit modules). Execute in order: Chunk 1 (Tasks 1–4) are runtime/safety fixes, Chunk 2 (Tasks 5–6) are audit/data polish. Each task is independently committable and testable.

After each task: build clean, run its targeted test, run full CTest, commit. After all six: run `backend/tests/test_mcp.sh` and `backend/tests/test_mcp_server.sh` end-to-end as the final gate.
