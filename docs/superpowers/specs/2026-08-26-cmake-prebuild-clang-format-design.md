# Design: Pre-build Auto clang-format for Backend

- **Date**: 2026-08-26
- **Status**: Approved
- **Scope**: `backend/CMakeLists.txt`, new `backend/cmake/FormatSources.cmake`

## Background

A repo-wide manual clang-format pass was recently applied to `backend/src`. The sweep accidentally
also reformatted the vendored dependency under `build/_deps/yyjson-src`, and clang-format's
`InsertBraces` corrupted goto-label code there, breaking the build until the dependency was
re-fetched. To prevent formatting drift without risking vendored code again, formatting should be
enforced automatically by the build system itself, scoped strictly to first-party sources.

## Goal

Every `cmake --build backend/build` invocation guarantees `backend/src/**/*.{c,h}` conforms to
`backend/.clang-format` before compilation starts, fixing violations in place automatically.

## Decision

**Behavior on unformatted code: auto-fix (`clang-format -i`), then continue compiling.**
(User-selected option.) Not fail-build, not warn-only.

Rejected alternatives:

| Approach | Why rejected |
|----------|--------------|
| Unconditional `-i` on every build | Rewrites every file each build → mtime churn → perpetual full recompiles |
| `add_custom_command(TARGET ... PRE_BUILD)` | PRE_BUILD ordering only works on VS generators; with Makefiles it fires post-compile (PRE_LINK) — too late |
| Fail-build / warn-only check | Contradicts chosen auto-fix behavior |

## Architecture

Two units with a single interface between them:

1. **Wiring unit — `CMakeLists.txt` addition (~15 lines)**
   - Discovers the tool: `find_program(CLANG_FORMAT_EXE NAMES clang-format clang-format-18 … clang-format-14)`.
   - If not found → `message(STATUS "clang-format not found - skipping pre-build auto-format")`
     and NO hook is registered. Docker/CI images lacking the binary keep building unchanged.
   - Collects inputs: `file(GLOB_RECURSE FORMAT_SOURCES "src/*.c" "src/*.h")`.
     Plain GLOB (no `CONFIGURE_DEPENDS`) — matches the existing `SOURCES` glob style at line 22.
   - Defines stamp output `${CMAKE_BINARY_DIR}/clang-format.stamp`; custom command
     `DEPENDS ${FORMAT_SOURCES} .clang-format` so any source edit or config edit triggers a
     re-check.
   - Registers `add_custom_target(format_sources ALL DEPENDS <stamp>)` and
     `add_dependencies(minefolio format_sources)` — guaranteed ordering with Makefiles:
     formatting completes before any compilation.
   - Passes tool path, space-joined absolute source list, and stamp path into the script via
     `-D` cache variables.

2. **Worker unit — `cmake/FormatSources.cmake` (~35 lines, run via `cmake -P`)**
   Interface: reads `CLANG_FORMAT`, `SOURCES`, `STAMP`; no other inputs.
   - Fast path: one `execute_process(clang-format --dry-run --Werror ${SOURCES})`.
     Exit 0 → tree clean → `touch STAMP`, done (single process, sub-second).
   - Slow path: dry-run exits non-zero → loop files individually, quiet dry-run per file;
     offenders get `-i` applied (only those files are rewritten — untouched files keep their
     mtimes, so no rebuild cascade); log each fix via `message(STATUS)`; then touch stamp.
     No re-verification after fixing: this is safe by construction because the SAME binary and
     config perform both the check and the fix — a comment in the script must state this
     invariant so nobody later adds a `--style` override on only one of the two paths.
   - Failure: if `-i` returns non-zero (broken binary or unreadable config) →
     `message(FATAL_ERROR "clang-format failed on <file>")`.

## Data Flow

```
cmake --build
  └─> format_sources target stale? (any src/*.c|*.h or .clang-format newer than stamp)
        ├─ no  → skip (zero cost)
        └─ yes → cmake -P FormatSources.cmake
              ├─ batch dry-run OK → touch stamp → done
              └─ dry-run fails    → per-file dry-run → -i offenders → touch stamp
                    └─> minefolio compiles (always sees formatted sources)
```

## Error Handling & Edge Cases

| Case | Behavior |
|------|----------|
| clang-format not installed | STATUS message, hook skipped entirely, build proceeds |
| Tool too old for config (`AlignTrailingComments: Kind:` needs ≥14) | dry-run errors → treated as offender scan failure → per-file pass also fails → FATAL_ERROR with file name; version discovery limited to ≥14 via NAMES list. Expected chain: a config-parse error makes EVERY file report as an offender in the per-file pass, and the first `-i` then fails with the same config error — this is intentional; do not special-case it away |
| Vendored code (`build/_deps/**`) | Out of scope by construction — glob rooted at `src/`, never matches `_deps` |
| New `.c/.h` file added | Picked up on next `cmake` reconfigure (same limitation as existing `SOURCES` glob — accepted) |
| File being rewritten concurrently by an editor | Same risk as any pre-build hook; `-i` is atomic-enough per file for a personal project — accepted |
| Stamp deleted manually | Next build re-runs one cheap dry-run, restores stamp |

## Testing / Verification Plan

1. Misformat a scratch change in one source file → `cmake --build` → expect STATUS log naming the
   fixed file, compile succeeds.
2. Immediately rebuild → expect format step to take the fast path (no file rewrites) and no
   recompilation cascade (`make` output shows up-to-date targets).
3. `touch backend/.clang-format` → rebuild → dry-run re-check runs.
4. Rename/remove clang-format from PATH in a scratch shell → configure fresh build dir → expect
   STATUS skip message and successful build.
5. Worker interface in isolation: invoke `cmake -P cmake/FormatSources.cmake` directly with
   `-DCLANG_FORMAT=… -DSOURCES=… -DSTAMP=…` against a scratch file list — verify fast path, fix
   path, and FATAL_ERROR path without going through the full build.
6. Full suite: existing `./tests/test_link.sh` still passes (behavioral safety net).

## Out of Scope

- Formatting frontend/TypeScript or mobile sources.
- CI-side format checking (redundant once builds self-fix).
- Upgrading the project to `CONFIGURE_DEPENDS` globs.
