#!/usr/bin/env bash
# scripts/release.sh — 发布管理与版本一致性检查
#
# 子命令:
#   check                    校验前端/后端/compose/git tag 版本号一致
#   list                     列出最近 release tag
#   changelog [--since TAG]  从 conventional commits 生成 markdown 变更日志
#
# 用法: ./release.sh <subcommand> [选项]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

usage() {
  cat <<'EOF'
用法: release.sh <subcommand> [选项]

子命令:
  check                校验 package.json / CMakeLists.txt / docker-compose.yml / git tag 版本一致
  list                 列出最近 release tag
  changelog [--since]  生成 markdown 变更日志（按 conventional commit 分组）

选项:
  --expected VERSION   指定期望版本（check 用）
  --since TAG          changelog 起始 tag（默认最近 tag）
  -h, --help           显示帮助
EOF
}

log()  { printf '[release] %s\n' "$*"; }
die()  { printf '[release] ERROR: %s\n' "$*" >&2; exit 1; }

[ "$#" -ge 1 ] || { usage; exit 1; }
case "$1" in -h|--help) usage; exit 0 ;; esac
CMD="$1"; shift

EXPECTED=""
SINCE=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --expected) [ "$#" -ge 2 ] || die "--expected 缺少参数"; EXPECTED="$2"; shift 2 ;;
    --since)    [ "$#" -ge 2 ] || die "--since 缺少参数"; SINCE="$2"; shift 2 ;;
    *) die "未知参数: $1" ;;
  esac
done

frontend_version() {
  sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$PROJECT_DIR/frontend/package.json" | head -1
}
backend_version() {
  sed -n 's/^project(minefolio VERSION \([^ )]*\).*/\1/p' "$PROJECT_DIR/backend/CMakeLists.txt" | head -1
}
compose_version() {
  sed -n 's/.*MINEFOLIO_VERSION:-\([^}]*\)}.*/\1/p' "$PROJECT_DIR/docker-compose.yml" | head -1
}
latest_tag() {
  git -C "$PROJECT_DIR" describe --tags --abbrev=0 2>/dev/null || true
}

case "$CMD" in
  list)
    git -C "$PROJECT_DIR" tag --list 'v*' --sort=-v:refname | head -20
    ;;

  check)
    FE="$(frontend_version)"
    BE="$(backend_version)"
    CO="$(compose_version)"
    TAG="$(latest_tag)"
    TAG_CLEAN="${TAG#v}"

    log "frontend/package.json : ${FE:-<missing>}"
    log "backend/CMakeLists.txt: ${BE:-<missing>}"
    log "docker-compose.yml    : ${CO:-<missing>}"
    log "latest git tag        : ${TAG:-<none>}"

    [ -n "$EXPECTED" ] && TAG_CLEAN="${EXPECTED#v}"

    MISMATCH=0
    for v in "$FE" "$BE" "$CO"; do
      [ -n "$v" ] || { die "版本号提取失败，请检查上述文件"; }
      if [ -n "$TAG_CLEAN" ] && [ "$v" != "$TAG_CLEAN" ]; then MISMATCH=1; fi
    done
    if [ -n "$TAG_CLEAN" ] && [ "$FE" != "$BE" ] || [ "$FE" != "$CO" ]; then MISMATCH=1; fi

    if [ "$MISMATCH" -ne 0 ]; then
      MSG="版本号不一致：请同步 package.json / CMakeLists.txt / docker-compose.yml"
      [ -n "$TAG_CLEAN" ] && MSG="$MSG 与 tag $TAG_CLEAN"
      die "$MSG"
    fi
    log "版本一致: ${FE}${TAG_CLEAN:+ (tag $TAG_CLEAN)}"
    ;;

  changelog)
    if [ -z "$SINCE" ]; then SINCE="$(latest_tag)"; fi
    RANGE="HEAD"
    if [ -n "$SINCE" ]; then
      git -C "$PROJECT_DIR" rev-parse --verify -q "$SINCE" >/dev/null || die "无效 tag: $SINCE"
      RANGE="${SINCE}..HEAD"
    fi
    log "生成变更日志: $RANGE"

    printf '## Changelog%s\n\n' "$( [ -n "$SINCE" ] && printf ' since %s' "$SINCE")"
    for spec in "feat:Features" "fix:Bug Fixes" "perf:Performance" "refactor:Refactoring" \
                "test:Tests" "docs:Documentation" "build:Build" "ci:CI" "chore:Chores"; do
      type="${spec%%:*}"
      title="${spec##*:}"
      entries="$(git -C "$PROJECT_DIR" log --no-merges --pretty=format:'%h %s' "$RANGE" \
        | grep -E "^[0-9a-f]+ ${type}(\\(|:)" || true)"
      if [ -n "$entries" ]; then
        printf '### %s\n\n' "$title"
        printf '%s\n' "$entries" | sed 's/^/- /'
        printf '\n'
      fi
    done
    ;;

  *) usage; die "未知子命令: $CMD" ;;
esac
