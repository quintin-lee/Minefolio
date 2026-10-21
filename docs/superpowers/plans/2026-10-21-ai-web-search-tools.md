# AI Web Search & Deep Reading Tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement zero-cost, API-key-free web search (`web_search`) and deep webpage reading (`fetch_webpage`) tools for Minefolio's AI assistant, supporting financial market news, macroeconomic data, general knowledge, and SSRF security defense.

**Architecture:** Two decoupled ReAct tools registered in the unified AI Tool Registry (`services/ai/tools/`). `web_search` scrapes live SERP snippets from Bing China with Wikipedia OpenSearch fallback and base64 redirect unpacking. `fetch_webpage` extracts cleaned Markdown via Jina Reader proxy (`r.jina.ai`) with local HTML tag-stripping fallback, guarded by strict SSRF validation and safe multi-hop redirect verification.

**Tech Stack:** C23, libcurl, csilk framework (`csilk_json_t`), POSIX regex/string processing, CMake/CTest.

**Spec:** [`docs/superpowers/specs/2026-10-21-ai-web-search-tools-design.md`](file:///data/home/quintin/workspace/source/c/Minefolio/docs/superpowers/specs/2026-10-21-ai-web-search-tools-design.md)

## Global Constraints

- C23 standard; GCC 14 with 0 compiler warnings.
- Strict DDD four-layer architecture: Domain layer (`domain/ai/rules.h/.c`) has zero external dependencies; Tools live in `services/ai/tools/`.
- Zero-cost: No API keys or paid external services required.
- SSRF prevention: Outgoing requests must strictly forbid `localhost`, `127.0.0.1`, `0.0.0.0`, `::1`, and private subnets `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, `169.254.0.0/16`.
- Anti-bypass redirect defense: Follow redirects manually (max 3 hops) with re-validation on each hop; never enable uninspected blanket `CURLOPT_FOLLOWLOCATION`.
- JSON safety: Always use `csilk_json_*` memory management with clean destruction (`csilk_json_free`).

## Review Focus

1. **Malicious SSRF Redirect Target**: An external URL redirects (301/302) to `http://127.0.0.1:8080/api/settings` or `http://192.168.1.1` — must be caught and blocked on the redirect hop.
2. **Long Web URLs**: A news URL with percent-encoded query parameters exceeds 512 bytes (up to 4096 bytes) — must not be falsely rejected by size limits.
3. **Bing Click-Tracking Encrypted URLs**: Bing wraps a link in `/ck/a?!...&u=a1<base64>` — must unpack base64url to obtain the canonical URL instead of returning tracking noise.
4. **Network Timeout / Hang**: An external target server hangs or drops packets — curl timeout (5000ms search, 8000ms fetch) must trigger cleanly and return an error JSON without hanging the server or worker thread.
5. **Token Context Budget Overflow**: A target webpage contains 50,000 characters — `fetch_webpage` must truncate content to `max_chars` (default 4000, capped at 10000) and set `"truncated": true`.

---

### Task 1: AI URL SSRF Validation in Domain Layer

**Files:**
- Modify: `backend/src/domain/ai/rules.h`
- Modify: `backend/src/domain/ai/rules.c`
- Test: `backend/tests/unit/test_ai_tools.c`

**Interfaces:**
- Consumes: None (pure C domain logic)
- Produces: `bool mf_ai_rule_validate_url(const char* url, size_t max_len)`

- [ ] **Step 1: Write failing unit test for `mf_ai_rule_validate_url` in `test_ai_tools.c`**

Add tests for:
- Valid public URLs (`https://blogs.nvidia.cn/blog/test`, `http://example.com/path?a=1&b=2`)
- Rejection of non-http/https schemas (`ftp://example.com`, `file:///etc/passwd`, `javascript:alert(1)`)
- Rejection of localhost and loopbacks (`http://localhost`, `http://127.0.0.1:8080`, `http://127.0.0.2`, `http://[::1]`)
- Rejection of private IPv4 subnets (`http://10.0.0.1`, `http://192.168.1.100`, `http://172.16.5.1`, `http://172.31.255.255`, `http://169.254.1.1`)
- Support for long URLs up to 4096 bytes and rejection beyond max_len.

- [ ] **Step 2: Run test to verify it fails to compile or link**

Run: `cd backend && cmake --build build --target test_ai_tools && ./build/tests/unit/test_ai_tools`
Expected: Compile error: `implicit declaration of function 'mf_ai_rule_validate_url'`

- [ ] **Step 3: Implement `mf_ai_rule_validate_url` in `backend/src/domain/ai/rules.h` and `rules.c`**

Add declaration to `rules.h`:
```c
/**
 * @brief 校验 AI 抓取目标的 URL 合法性与 SSRF 安全性
 * 规则：必须为 http:// 或 https:// 开头；长度不超过 max_len（默认上限 4096）；
 *       host 不得为内网/回环地址（localhost/127./10./172.16-31./192.168./169.254./::1/0.0.0.0）。
 */
bool mf_ai_rule_validate_url(const char* url, size_t max_len);
```

Implement in `rules.c` reusing standard private IP parsing logic.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd backend && cmake --build build --target test_ai_tools && ./build/tests/unit/test_ai_tools`
Expected: PASS with SSRF validation assertions verified.

- [ ] **Step 5: Commit**

```bash
git add backend/src/domain/ai/rules.h backend/src/domain/ai/rules.c backend/tests/unit/test_ai_tools.c
git commit -m "feat(ai): add mf_ai_rule_validate_url with SSRF prevention in domain layer"
```

---

### Task 2: Implement Web Search & Deep Fetch Tools

**Files:**
- Create: `backend/src/services/ai/tools/web_search_tool.h`
- Create: `backend/src/services/ai/tools/web_search_tool.c`
- Modify: `backend/src/services/ai/tools/registry.c`
- Test: `backend/tests/unit/test_ai_tools.c`

**Interfaces:**
- Consumes: `mf_ai_rule_validate_url(url, 4096)`, `ai_tool_register(tool)`, `csilk_json_*`, `libcurl`
- Produces:
  - `void ai_tool_web_search_register_all(void)`
  - Tool `"web_search"` (inputs: `query`, optional `num_results`)
  - Tool `"fetch_webpage"` (inputs: `url`, optional `max_chars`)

- [ ] **Step 1: Write unit tests for `web_search` and `fetch_webpage` in `test_ai_tools.c`**

Add test cases in `test_ai_tools.c`:
1. Tool registry verification: confirm `ai_tool_find("web_search")` and `ai_tool_find("fetch_webpage")` return non-NULL with correct permissions (`AI_PERM_READ`, `AI_RISK_LOW`, `is_mutation = false`).
2. Schema validation:
   - `web_search` with missing `query` -> returns error JSON.
   - `fetch_webpage` with missing `url` -> returns error JSON.
3. SSRF rejection via tool dispatcher:
   - `fetch_webpage` with `url: "http://127.0.0.1:8080/api/users"` -> returns `forbidden_url`.
   - `fetch_webpage` with `url: "http://192.168.1.1/admin"` -> returns `forbidden_url`.
4. Mock SERP parser & Bing base64 URL unpacker:
   - Feed sample Bing HTML containing `<h2><a href="...">` and `<div class="b_caption"><p class="b_lineclamp2">` to verify extracted JSON matches expectations.
   - Feed Bing tracking link with `u=a1<base64>` to verify it extracts the canonical URL.

- [ ] **Step 2: Run test to verify it fails**

Run: `cd backend && cmake --build build --target test_ai_tools && ./build/tests/unit/test_ai_tools`
Expected: Compile error: `web_search_tool.h: No such file or directory`

- [ ] **Step 3: Implement `web_search_tool.h` and `web_search_tool.c`**

Create `backend/src/services/ai/tools/web_search_tool.h`:
```c
#pragma once
#include "services/ai/tools/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

void ai_tool_web_search_register_all(void);

#ifdef __cplusplus
}
#endif
```

Implement `backend/src/services/ai/tools/web_search_tool.c`:
1. `curl_http_get_with_redirect_check()`:
   - Performs HTTP GET with timeout (5000ms default).
   - Sets Chrome User-Agent and `Accept-Language`.
   - Checks `CURLOPT_FOLLOWLOCATION = 0L`.
   - Inspects HTTP 3xx responses: reads `Location:` header, validates with `mf_ai_rule_validate_url()`, and loops up to 3 hops.
2. String & HTML cleaning helpers:
   - `html_strip_tags()`: strips `<script>`, `<style>`, and standard tags.
   - `html_unescape()`: decodes `&amp;`, `&quot;`, `&#39;`, `&lt;`, `&gt;`, `&nbsp;`.
   - `bing_unpack_url()`: decodes `u=a1...` base64url payload when `/ck/a?` is encountered.
   - `bing_html_parse()`: parses `<li class="b_algo">...` into title, url, snippet JSON array.
   - `wikipedia_opensearch()` fallback: parses Wikipedia JSON array response `[query, [titles], [snippets], [urls]]`.
3. Tool execution handlers:
   - `exec_web_search()`: queries Bing SERP, falls back to Wikipedia, serializes results JSON.
   - `exec_fetch_webpage()`: checks SSRF, queries Jina Reader proxy (`https://r.jina.ai/{url}`) with text accept, falls back to direct curl with HTML tag stripper, truncates to `max_chars`.
4. Register tools in `ai_tool_web_search_register_all()`.

Register in `backend/src/services/ai/tools/registry.c`:
- Call `ai_tool_web_search_register_all()` in `registry_init_impl()`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd backend && cmake --build build --target test_ai_tools && ./build/tests/unit/test_ai_tools`
Expected: ALL AI tool tests pass (including web_search and fetch_webpage).

- [ ] **Step 5: Commit**

```bash
git add backend/src/services/ai/tools/web_search_tool.h backend/src/services/ai/tools/web_search_tool.c backend/src/services/ai/tools/registry.c backend/tests/unit/test_ai_tools.c
git commit -m "feat(ai): implement web_search and fetch_webpage tools with Jina Reader and Bing SERP"
```

---

### Task 3: Comprehensive Regression Verification & Real Query Validation

**Files:**
- Verify: Full backend test suites and frontend build.

- [ ] **Step 1: Run all unit test suites in backend**

Run: `cd backend/build && ctest --output-on-failure`
Expected: 100% of test suites pass (35+ suites).

- [ ] **Step 2: Run real search and deep fetch live verification**

Run a lightweight C verification check or CLI query verifying:
1. `web_search` with `"美联储 利率"` returns real live news results.
2. `fetch_webpage` on one of the search results returns structured article markdown.

- [ ] **Step 3: Run frontend build check**

Run: `npm --prefix frontend run build`
Expected: 0 TypeScript and build errors.

- [ ] **Step 4: Commit any final test fixtures or adjustments**

```bash
git commit --allow-empty -m "chore(ai): verify web search tools and regression suites"
```
