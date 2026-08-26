# CMake Pre-build Auto clang-format Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every backend build auto-fixes `backend/src/**/*.{c,h}` to conform to `backend/.clang-format` before compilation.

**Architecture:** A stamp-gated CMake custom command (fast path: one batch `--dry-run`; slow path: per-file `-i` on offenders only) wired as a dependency of the `minefolio` target. Formatting logic lives in a standalone `cmake -P` script so it is testable in isolation.

**Tech Stack:** CMake ≥3.16 (Unix Makefiles), clang-format ≥14 (required by `.clang-format`'s `AlignTrailingComments: Kind:` syntax).

**Spec:** `docs/superpowers/specs/2026-08-26-cmake-prebuild-clang-format-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `backend/cmake/FormatSources.cmake` | Create | Worker: check-and-fix sources, manage stamp. Interface: `-DCLANG_FORMAT`, `-DSOURCES`, `-DSTAMP`. |
| `backend/CMakeLists.txt` | Modify (~20 lines appended after line 35) | Wiring: tool discovery, source glob, custom command/target, ordering dependency. |

No other files change. Vendored code under `build/_deps/**` is unreachable by construction (globs rooted at `src/`).

---

## Chunk 1: Worker Script

### Task 1: Create `FormatSources.cmake` and verify its three paths standalone

**Files:**
- Create: `backend/cmake/FormatSources.cmake`

- [ ] **Step 1: Write the worker script**

Create `backend/cmake/FormatSources.cmake` with exactly:

```cmake
# Pre-build auto-format worker. Invoked from CMakeLists.txt via:
#   cmake -DCLANG_FORMAT=<path> "-DSOURCES=<space-joined abs paths>" -DSTAMP=<path> -P FormatSources.cmake
#
# Invariant: the SAME clang-format binary and .clang-format config perform both
# the check (--dry-run) and the fix (-i). Do NOT add a --style override on only
# one of the two paths, or fixed output could still fail the check while the
# stamp hides the drift.

separate_arguments(SOURCES)

execute_process(
    COMMAND "${CLANG_FORMAT}" --dry-run --Werror ${SOURCES}
    RESULT_VARIABLE batch_result
    OUTPUT_QUIET
    ERROR_QUIET
)

if(batch_result EQUAL 0)
    # Fast path: everything already conforms.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E touch "${STAMP}")
    return()
endif()

# Slow path: locate offenders individually; rewrite ONLY those files so
# untouched files keep their mtimes (avoids full-recompile cascades).
# NOTE: a config-parse failure makes EVERY file look like an offender here;
# the first -i then fails with the same config error -> FATAL_ERROR below.
# That chain is intentional. Do not special-case it.
foreach(src IN LISTS SOURCES)
    execute_process(
        COMMAND "${CLANG_FORMAT}" --dry-run --Werror "${src}"
        RESULT_VARIABLE file_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT file_result EQUAL 0)
        execute_process(
            COMMAND "${CLANG_FORMAT}" -i "${src}"
            RESULT_VARIABLE fix_result
        )
        if(NOT fix_result EQUAL 0)
            message(FATAL_ERROR "clang-format failed on ${src}")
        endif()
        message(STATUS "clang-formatted ${src}")
    endif()
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E touch "${STAMP}")
```

- [ ] **Step 2: Verify fix path standalone**

```bash
mkdir -p /tmp/opencode/fmt-test/src
printf 'int main(){return 0;}\n' > /tmp/opencode/fmt-test/src/a.c
# -D must precede -P: args after -P become CMAKE_ARGV*, not cache vars.
# .clang-format sits at the scratch root so clang-format finds it searching
# upward from src/a.c.
cp backend/.clang-format /tmp/opencode/fmt-test/.clang-format
cmake -DCLANG_FORMAT="$(command -v clang-format)" \
    -DSOURCES="/tmp/opencode/fmt-test/src/a.c" \
    -DSTAMP=/tmp/opencode/fmt-test/stamp \
    -P backend/cmake/FormatSources.cmake
cat /tmp/opencode/fmt-test/src/a.c
ls /tmp/opencode/fmt-test/stamp
```

Expected: STATUS line `-- clang-formatted /tmp/opencode/fmt-test/src/a.c`; file content becomes
(return type on its own line from `BreakBeforeBraces: Linux` +
`AlwaysBreakAfterReturnType: AllDefinitions`; braces inserted by `InsertBraces`):

```c
int
main()
{
    return 0;
}
```

and `stamp` exists.

- [ ] **Step 3: Verify fast path standalone**

```bash
cmake -DCLANG_FORMAT="$(command -v clang-format)" \
    -DSOURCES="/tmp/opencode/fmt-test/src/a.c" \
    -DSTAMP=/tmp/opencode/fmt-test/stamp2 \
    -P backend/cmake/FormatSources.cmake
echo "exit=$?"
```

Expected: no `clang-formatted` STATUS line, `exit=0`, `stamp2` exists.

- [ ] **Step 4: Verify FATAL_ERROR path standalone**

```bash
printf 'int main(){return 0;}\n' > /tmp/opencode/fmt-test/src/b.c
cmake -DCLANG_FORMAT=/bin/false \
    -DSOURCES="/tmp/opencode/fmt-test/src/b.c" \
    -DSTAMP=/tmp/opencode/fmt-test/stamp3 \
    -P backend/cmake/FormatSources.cmake
echo "exit=$?"
```

Expected: `CMake Error: clang-format failed on /tmp/opencode/fmt-test/src/b.c`, `exit=1`.

- [ ] **Step 5: Clean up scratch**

```bash
rm -rf /tmp/opencode/fmt-test
```

- [ ] **Step 6: Commit**

```bash
git add backend/cmake/FormatSources.cmake
git commit -m "build(backend): 📦 add pre-build clang-format worker script"
```

---

## Chunk 2: CMake Wiring + Integration Verification

### Task 2: Wire hook into `CMakeLists.txt`

**Files:**
- Modify: `backend/CMakeLists.txt` (insert after the `PQ_INCLUDE_DIRS` block ending at line 35; MUST be after `add_executable(minefolio ...)` at line 23 because `add_dependencies` requires the target to exist)

- [ ] **Step 1: Append the wiring block**

Insert into `backend/CMakeLists.txt` immediately after the `target_include_directories(... ${PQ_INCLUDE_DIRS})` block:

```cmake
# ---------------------------------------------------------------------------
# Pre-build auto-format: keep src/**/*.{c,h} conformant to .clang-format
# Requires clang-format >= 14 (.clang-format uses AlignTrailingComments Kind:)
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT_EXE
    NAMES clang-format clang-format-18 clang-format-17 clang-format-16
          clang-format-15 clang-format-14
)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE FORMAT_SOURCES "src/*.c" "src/*.h")
    set(FORMAT_STAMP "${CMAKE_BINARY_DIR}/clang-format.stamp")
    add_custom_command(
        OUTPUT "${FORMAT_STAMP}"
        COMMAND ${CMAKE_COMMAND}
            -DCLANG_FORMAT="${CLANG_FORMAT_EXE}"
            "-DSOURCES=${FORMAT_SOURCES}"
            -DSTAMP="${FORMAT_STAMP}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FormatSources.cmake"
        DEPENDS ${FORMAT_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/.clang-format"
        COMMENT "Checking source formatting"
        VERBATIM
    )
    add_custom_target(format_sources ALL DEPENDS "${FORMAT_STAMP}")
    add_dependencies(minefolio format_sources)
else()
    message(STATUS "clang-format not found - skipping pre-build auto-format")
endif()
```

Notes:
- Plain `file(GLOB_RECURSE ...)` (no `CONFIGURE_DEPENDS`) — matches the existing style at line 22.
- `.clang-format` is in `DEPENDS`: editing the config triggers a full re-check.

- [ ] **Step 2: Reconfigure and build — verify clean-tree fast path**

```bash
cmake -B backend/build -S backend -G "Unix Makefiles" 2>&1 | tail -3
cmake --build backend/build --parallel
```

Expected configure output includes `-- clang-format not found - skipping...` **only if** tool absent;
with tool present: no such line. Expected build output includes `Checking source formatting`
once, then `Built target minefolio` (percentage may shift due to the new ALL target), exit 0.

- [ ] **Step 3: Verify no-op rebuild (no recompile cascade)**

```bash
set -o pipefail   # so $? reflects cmake's status through tee
cmake --build backend/build 2>&1 | tee /tmp/opencode/rebuild.log
echo "exit=$?"
grep -c 'Checking source formatting' /tmp/opencode/rebuild.log || true
grep -c 'Building C object.*src/' /tmp/opencode/rebuild.log || true
```

Expected: `exit=0`; zero occurrences of both patterns (stamp up-to-date → format step skipped,
nothing recompiled).

- [ ] **Step 4: Verify auto-fix on a deliberately broken source**

```bash
# NOTE: 8 spaces — all `return -1;` lines in balance.c sit at 8+ space depth;
# a tab indent violates UseTab: Never, clang-format restores it byte-identically.
sed -i 's/^        return -1;/\treturn -1;/' backend/src/common/balance.c
cmake --build backend/build 2>&1 | grep -E 'clang-formatted|Built target'
git diff --stat backend/src/common/balance.c
```

Expected: build log shows `clang-formatted .../common/balance.c` then builds successfully;
`git diff` on balance.c is EMPTY afterwards (fixed back to identical committed content).

- [ ] **Step 5: Verify `.clang-format` edit triggers re-check**

```bash
touch backend/.clang-format
cmake --build backend/build 2>&1 | grep -c 'Checking source formatting'
```

Expected: `1` (dry-run re-check ran; no `clang-formatted` lines since tree is clean).

- [ ] **Step 6: Commit**

```bash
git add backend/CMakeLists.txt
git commit -m "build(backend): 📦 wire pre-build clang-format hook into build"
```

---

## Chunk 3: Regression & Missing-Tool Verification

### Task 3: Edge cases + full regression

**Files:** none created/modified (verification only)

- [ ] **Step 1: Verify skip path when tool absent**

Build a PATH clone containing every executable EXCEPT `clang-format*`, then run BOTH configure and
build inside that restricted environment (naively dropping a whole dir like `/usr/bin` would also
hide `cc`/`make` and break configuration):

```bash
mkdir -p /tmp/opencode/nofmtbin
IFS=':' read -ra DIRS <<< "$PATH"
for d in "${DIRS[@]}"; do
    [ -d "$d" ] || continue
    for f in "$d"/*; do
        [ -e "$f" ] || continue
        b="$(basename "$f")"
        case "$b" in clang-format*) continue ;; esac
        [ -e "/tmp/opencode/nofmtbin/$b" ] || ln -s "$f" "/tmp/opencode/nofmtbin/$b" 2>/dev/null || true
    done
done
export MF_ROOT="$PWD"
env PATH="/tmp/opencode/nofmtbin" bash -c '
    hash -r
    command -v clang-format && { echo "FAIL: still visible"; exit 1; } || echo ABSENT
    cmake -B /tmp/opencode/nofmt-build -S "$MF_ROOT/backend" -G "Unix Makefiles" > /tmp/opencode/nofmt-config.log 2>&1
    grep -i "clang-format not found" /tmp/opencode/nofmt-config.log
    cmake --build /tmp/opencode/nofmt-build --parallel > /tmp/opencode/nofmt-build.log 2>&1
    tail -1 /tmp/opencode/nofmt-build.log
'
rm -rf /tmp/opencode/nofmt-build /tmp/opencode/nofmtbin /tmp/opencode/nofmt-*.log
```

Expected: `ABSENT`; grep prints `-- clang-format not found - skipping pre-build auto-format`;
final build line is `Built target minefolio`.

- [ ] **Step 2: Run full backend integration suite**

```bash
./tests/test_link.sh
```

Expected: `PASS=33  FAIL=0` (or all-pass summary; suite exits 0).

- [ ] **Step 3: Confirm working tree state**

```bash
git status --porcelain
```

Expected: empty (all changes committed in Tasks 1–2; verification mutated nothing persistent).
If anything dirty appeared during Step 4 of Task 2 that didn't self-heal, investigate before
committing — never commit a half-fixed file.

---

## Verification Matrix (spec traceability)

| Spec requirement | Verified by |
|---|---|
| Auto-fix before compile | Task 2 Step 4 |
| No mtime-churn rebuild cascade | Task 2 Step 3 |
| `.clang-format` in DEPENDS | Task 2 Step 5 |
| Missing tool → skip gracefully | Task 3 Step 1 |
| Worker fast/slow/fatal paths | Task 1 Steps 2–4 |
| Behavioral regression safety | Task 3 Step 2 |
