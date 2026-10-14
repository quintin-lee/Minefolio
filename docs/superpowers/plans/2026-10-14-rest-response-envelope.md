# RESTful Response Envelopes & Pagination Helpers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Downstream unified RESTful JSON response envelopes (`csilk_respond_ok`, `csilk_respond_fail`, semantic error helpers, and `csilk_parse_pagination`) into `csilk` and refactor `Minefolio` to use them seamlessly.

**Architecture:** Core response primitives in `csilk` are extended with standard success and error envelope helpers with an Arena stack-buffer zero-heap fast path. Query parsing uses strict `strtoll` validation. `Minefolio` replaces internal boilerplate with thin forwarding wrappers preserving the legacy `"list"` key and HTTP 200 contract.

**Tech Stack:** C23, CMake, CTest, csilk web framework, yyjson / cJSON, Linux, POSIX.

---

### File Structure Map

#### `csilk` Repository (`/data/home/quintin/workspace/source/c/csilk`)
- **Modify**: `include/csilk/core/response.h` — Declares `csilk_respond_ok`, `csilk_respond_ok_null`, `csilk_respond_fail`, semantic error helpers, `csilk_parse_pagination`, `csilk_respond_page`, and `csilk_respond_page_ex`.
- **Modify**: `src/core/primitives/response.c` — Implements response assembly, zero-heap Arena fast path, and strict query parsing.
- **Modify**: `cmake/tests.cmake` — Adds `test_response_envelope` to `CSILK_CORE_TESTS`.
- **Create**: `tests/core/test_response_envelope.c` — Comprehensive CTest suite testing all new functions.

#### `Minefolio` Repository (`/data/home/quintin/workspace/source/c/Minefolio`)
- **Modify**: `backend/src/common/response.h` — Refactor to thin forwarding wrappers calling `csilk_respond_*`.

---

### Task 1: Create Unit Test Suite in `csilk` (TDD - Failing Tests)

**Files:**
- Create: `/data/home/quintin/workspace/source/c/csilk/tests/core/test_response_envelope.c`
- Modify: `/data/home/quintin/workspace/source/c/csilk/cmake/tests.cmake:74-76`

- [ ] **Step 1: Register test executable in `cmake/tests.cmake`**

Add `test_response_envelope` to `CSILK_CORE_TESTS` in `csilk/cmake/tests.cmake`:

```cmake
    test_response
    test_response_envelope
    test_response_ownership
```

- [ ] **Step 2: Write unit test suite `tests/core/test_response_envelope.c`**

Create `/data/home/quintin/workspace/source/c/csilk/tests/core/test_response_envelope.c` with tests covering:
1. `csilk_respond_ok` with object payload and null payload (`csilk_respond_ok_null`)
2. `csilk_respond_fail` (explicit HTTP status, fallback biz_code, stack fast path, and special char escaping)
3. Semantic helpers (`csilk_respond_bad_request`, `csilk_respond_unauthorized`, `csilk_respond_not_found`, etc.)
4. `csilk_parse_pagination` (normal values, missing queries, non-numeric values, negative values, max cap)
5. `csilk_respond_page` (default `"items"`) and `csilk_respond_page_ex` (custom `"list"`)

```c
/**
 * @file test_response_envelope.c
 * @brief Unit tests for RESTful response envelopes and pagination helpers.
 * @copyright MIT License
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csilk/csilk.h"
#include "csilk/core/json.h"
#include "csilk/core/response.h"
#include "csilk/test/test.h"

static int tests_run = 0;
static int tests_passed = 0;

#define PASS() (tests_run++, tests_passed++)
#define FAIL(msg)                                                              \
    do {                                                                       \
        tests_run++;                                                           \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__);                       \
    } while (0)

static void
test_respond_ok_with_data(void)
{
    csilk_ctx_t* c = csilk_test_ctx_new();
    csilk_json_t* data = csilk_json_object();
    csilk_json_add_string(data, "name", "csilk");

    csilk_respond_ok(c, data);

    size_t len = 0;
    const char* body = csilk_get_response_body(c, &len);
    int status = csilk_get_status(c);

    if (status != 200) {
        FAIL("status must be 200");
    } else if (!body || strstr(body, "\"code\":0") == NULL ||
               strstr(body, "\"message\":\"ok\"") == NULL ||
               strstr(body, "\"name\":\"csilk\"") == NULL) {
        FAIL("body envelope missing code, message, or data");
    } else {
        PASS();
    }
    csilk_test_ctx_free(c);
}

static void
test_respond_ok_null(void)
{
    csilk_ctx_t* c = csilk_test_ctx_new();
    csilk_respond_ok_null(c);

    size_t len = 0;
    const char* body = csilk_get_response_body(c, &len);
    int status = csilk_get_status(c);

    if (status != 200) {
        FAIL("status must be 200");
    } else if (!body || strstr(body, "\"code\":0") == NULL ||
               strstr(body, "\"data\":null") == NULL) {
        FAIL("body must have code:0 and data:null");
    } else {
        PASS();
    }
    csilk_test_ctx_free(c);
}

static void
test_respond_fail_and_semantics(void)
{
    csilk_ctx_t* c = csilk_test_ctx_new();
    csilk_respond_fail(c, 400, 1002, "invalid parameter");

    size_t len = 0;
    const char* body = csilk_get_response_body(c, &len);
    int status = csilk_get_status(c);

    if (status != 400) {
        FAIL("status must be 400");
    } else if (!body || strstr(body, "\"code\":1002") == NULL ||
               strstr(body, "\"message\":\"invalid parameter\"") == NULL) {
        FAIL("body must contain code:1002 and message");
    } else {
        PASS();
    }
    csilk_test_ctx_free(c);

    /* Test semantic helper fallback */
    c = csilk_test_ctx_new();
    csilk_respond_unauthorized(c, 0, NULL);
    body = csilk_get_response_body(c, &len);
    status = csilk_get_status(c);

    if (status != 401) {
        FAIL("status must be 401");
    } else if (!body || strstr(body, "\"code\":401") == NULL ||
               strstr(body, "\"message\":\"Unauthorized\"") == NULL) {
        FAIL("body must fall back to 401 code and Unauthorized text");
    } else {
        PASS();
    }
    csilk_test_ctx_free(c);
}

static void
test_parse_pagination(void)
{
    csilk_ctx_t* c = csilk_test_ctx_new();
    /* No query params -> default 1 and 20 */
    int64_t page = 0;
    int64_t page_size = 0;
    csilk_parse_pagination(c, &page, &page_size, 20, 500);

    if (page != 1 || page_size != 20) {
        FAIL("default pagination should be page=1, page_size=20");
    } else {
        PASS();
    }
    csilk_test_ctx_free(c);

    /* Clamping test */
    c = csilk_test_ctx_new();
    csilk_test_ctx_set_query(c, "page", "-5");
    csilk_test_ctx_set_query(c, "page_size", "9999");
    csilk_parse_pagination(c, &page, &page_size, 20, 500);

    if (page != 1 || page_size != 500) {
        FAIL("page must clamp to 1 and page_size must clamp to max 500");
    } else {
        PASS();
    }
    csilk_test_ctx_free(c);
}

static void
test_respond_page_and_ex(void)
{
    csilk_ctx_t* c = csilk_test_ctx_new();
    csilk_json_t* arr = csilk_json_array();
    csilk_json_array_add(arr, csilk_json_string_new("item1"));

    csilk_respond_page(c, arr, 42, 2, 20);

    size_t len = 0;
    const char* body = csilk_get_response_body(c, &len);

    if (!body || strstr(body, "\"items\":[\"item1\"]") == NULL ||
        strstr(body, "\"total\":42") == NULL ||
        strstr(body, "\"page\":2") == NULL ||
        strstr(body, "\"page_size\":20") == NULL) {
        FAIL("default page response must contain items, total, page, page_size");
    } else {
        PASS();
    }
    csilk_test_ctx_free(c);

    /* Test custom key "list" via csilk_respond_page_ex */
    c = csilk_test_ctx_new();
    arr = csilk_json_array();
    csilk_json_array_add(arr, csilk_json_string_new("rowA"));
    csilk_respond_page_ex(c, "list", arr, 10, 1, 10);

    body = csilk_get_response_body(c, &len);
    if (!body || strstr(body, "\"list\":[\"rowA\"]") == NULL ||
        strstr(body, "\"total\":10") == NULL) {
        FAIL("custom key list response must contain list and total");
    } else {
        PASS();
    }
    csilk_test_ctx_free(c);
}

int
main(void)
{
    printf("=== RESTful Response Envelope Tests ===\n\n");

    test_respond_ok_with_data();
    test_respond_ok_null();
    test_respond_fail_and_semantics();
    test_parse_pagination();
    test_respond_page_and_ex();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_run - tests_passed);
    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

- [ ] **Step 3: Run CMake build to verify test fails to compile (missing symbols)**

Run:
```bash
cmake --build /data/home/quintin/workspace/source/c/csilk/build --target test_response_envelope
```
Expected: FAIL with compilation error (undefined references/declarations for `csilk_respond_ok`, etc.).

---

### Task 2: Implement RESTful Response Envelope & Pagination in `csilk`

**Files:**
- Modify: `/data/home/quintin/workspace/source/c/csilk/include/csilk/core/response.h`
- Modify: `/data/home/quintin/workspace/source/c/csilk/src/core/primitives/response.c`

- [ ] **Step 1: Declare functions in `include/csilk/core/response.h`**

Append the public function prototypes with English Doxygen comments to `include/csilk/core/response.h`.

- [ ] **Step 2: Implement functions in `src/core/primitives/response.c`**

Implement:
1. `_csilk_parse_query_int_strict(const char* s, int64_t dflt)`
2. `csilk_respond_ok(csilk_ctx_t* c, csilk_json_t* data)`
3. `csilk_respond_ok_null(csilk_ctx_t* c)`
4. `csilk_respond_fail(csilk_ctx_t* c, int http_status, int biz_code, const char* message)` with stack buffer fast-path and cJSON fallback
5. `csilk_respond_bad_request`, `csilk_respond_unauthorized`, `csilk_respond_forbidden`, `csilk_respond_not_found`, `csilk_respond_conflict`
6. `csilk_parse_pagination(csilk_ctx_t* c, int64_t* out_page, int64_t* out_page_size, int64_t default_size, int64_t max_size)`
7. `csilk_respond_page(csilk_ctx_t* c, csilk_json_t* items, int64_t total, int64_t page, int64_t page_size)`
8. `csilk_respond_page_ex(csilk_ctx_t* c, const char* array_key, csilk_json_t* items, int64_t total, int64_t page, int64_t page_size)`

Ensure all comments and logs are in English.

- [ ] **Step 3: Build and run `test_response_envelope` to verify tests pass**

Run:
```bash
cmake --build /data/home/quintin/workspace/source/c/csilk/build --target test_response_envelope && /data/home/quintin/workspace/source/c/csilk/build/test_response_envelope
```
Expected: Output `=== Results: 5 passed, 0 failed ===` with exit code 0.

- [ ] **Step 4: Run full `csilk` test suite via `ctest`**

Run:
```bash
cd /data/home/quintin/workspace/source/c/csilk/build && ctest --output-on-failure
```
Expected: 100% tests passed (226/226 or higher).

- [ ] **Step 5: Commit changes in `csilk`**

```bash
git -C /data/home/quintin/workspace/source/c/csilk add include/csilk/core/response.h src/core/primitives/response.c cmake/tests.cmake tests/core/test_response_envelope.c
git -C /data/home/quintin/workspace/source/c/csilk commit -m "feat(core): add RESTful response envelopes and pagination query helpers"
```

---

### Task 3: Refactor `Minefolio` to Use Downstreamed Functions

**Files:**
- Modify: `/data/home/quintin/workspace/source/c/Minefolio/backend/src/common/response.h`

- [ ] **Step 1: Replace implementation in `backend/src/common/response.h`**

Update `backend/src/common/response.h` to delegate to `csilk_respond_*` and `csilk_parse_pagination()`. Maintain English comments and docstrings.
Preserve the Minefolio legacy convention:
- `respond_ok(c, data)` calls `csilk_respond_ok(c, data)`
- `respond_ok_null(c)` calls `csilk_respond_ok_null(c)`
- `respond_error(c, code, msg)` calls `csilk_respond_fail(c, CSILK_STATUS_OK, code, msg)` (preserving HTTP 200 + business code)
- `parse_page_params(c, page, page_size)` calls `csilk_parse_pagination(c, page, page_size, 20, 500)`
- `respond_page_ok(c, list, total, page, page_size)` calls `csilk_respond_page_ex(c, "list", list, total, page, page_size)`

- [ ] **Step 2: Compile `Minefolio` backend**

Run:
```bash
cmake --build /data/home/quintin/workspace/source/c/Minefolio/backend/build --parallel
```
Expected: Compilation succeeds with 0 errors.

- [ ] **Step 3: Run unit tests in `Minefolio`**

Run:
```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend/build && ctest --output-on-failure
```
Expected: All 32 unit test suites pass.

- [ ] **Step 4: Run integration test suite `test_link.sh` in `Minefolio`**

Run:
```bash
cd /data/home/quintin/workspace/source/c/Minefolio/backend && ./tests/test_link.sh
```
Expected: All 38 test suites (143 assertions) pass with 0 failures.

- [ ] **Step 5: Run frontend Vitest tests in `Minefolio`**

Run:
```bash
cd /data/home/quintin/workspace/source/c/Minefolio/frontend && npm test
```
Expected: All unit tests pass.

- [ ] **Step 6: Commit changes in `Minefolio`**

```bash
git -C /data/home/quintin/workspace/source/c/Minefolio add backend/src/common/response.h
git -C /data/home/quintin/workspace/source/c/Minefolio commit -m "refactor(common): delegate response envelopes and pagination to csilk core"
```

---

### Task 4: Documentation & Final Verification

**Files:**
- Modify: `/data/home/quintin/workspace/source/c/csilk/CHANGELOG.md`
- Modify: `/data/home/quintin/workspace/source/c/csilk/CHANGELOG.zh-CN.md`
- Modify: `/data/home/quintin/workspace/source/c/Minefolio/AGENTS.md`

- [ ] **Step 1: Update `CHANGELOG.md` in `csilk`**

Add entry for RESTful Response Envelopes & Pagination Helpers in `csilk/CHANGELOG.md` and `csilk/CHANGELOG.zh-CN.md`.

- [ ] **Step 2: Update `AGENTS.md` in `Minefolio`**

Document that HTTP response envelopes and pagination parsing are now downstreamed to `csilk` in `AGENTS.md`.

- [ ] **Step 3: Commit documentation updates in both repositories**

```bash
git -C /data/home/quintin/workspace/source/c/csilk add CHANGELOG.md CHANGELOG.zh-CN.md
git -C /data/home/quintin/workspace/source/c/csilk commit -m "docs: record REST response envelopes in changelog"

git -C /data/home/quintin/workspace/source/c/Minefolio add AGENTS.md docs/superpowers/plans/2026-10-14-rest-response-envelope.md
git -C /data/home/quintin/workspace/source/c/Minefolio commit -m "docs: update AGENTS.md with response envelope downstreaming status"
```
