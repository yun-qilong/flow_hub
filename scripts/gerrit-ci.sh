#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<EOF
用法: $0 [选项]

选项:
  --ref REF            Gerrit patchset ref (如 refs/changes/01/101/1)
  --project PROJECT    Gerrit 项目名 (默认: flow_hub)
  --branch BRANCH      目标分支 (默认: main)
  --gerrit-host HOST   Gerrit 主机 (默认: localhost)
  --gerrit-port PORT   Gerrit SSH 端口 (默认: 29418)
  --gerrit-user USER   Gerrit SSH 用户 (默认: qilyun)
  --build-dir DIR      构建目录 (默认: /tmp/gerrit-ci-build)
  --help               显示帮助
EOF
  exit 0
}

# ---- 参数解析 -------------------------------------------------------
GERRIT_REF=""
PROJECT="${GERRIT_PROJECT:-flow_hub}"
BRANCH="${GERRIT_BRANCH:-main}"
GERRIT_HOST="${GERRIT_HOST:-localhost}"
GERRIT_PORT="${GERRIT_PORT:-29418}"
GERRIT_USER="${GERRIT_USER:-qilyun}"
BUILD_DIR="${BUILD_DIR:-/tmp/gerrit-ci-build}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ref) GERRIT_REF="$2"; shift 2 ;;
    --project) PROJECT="$2"; shift 2 ;;
    --branch) BRANCH="$2"; shift 2 ;;
    --gerrit-host) GERRIT_HOST="$2"; shift 2 ;;
    --gerrit-port) GERRIT_PORT="$2"; shift 2 ;;
    --gerrit-user) GERRIT_USER="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --help) usage ;;
    *) echo "未知选项: $1"; exit 1 ;;
  esac
done

RESULT_FILE="${BUILD_DIR}/ci-result.json"
GERRIT_URL="ssh://${GERRIT_USER}@${GERRIT_HOST}:${GERRIT_PORT}/${PROJECT}"

# ---- 工具函数 -------------------------------------------------------
FAILED_COUNT=0

run_check() {
  local name="$1"
  local category="$2"
  shift 2
  local start_time
  local end_time
  local exit_code

  start_time=$(date +%s)
  set +e
  "$@" > "${BUILD_DIR}/${name}.log" 2>&1
  exit_code=$?
  set -e
  end_time=$(date +%s)

  local status="SUCCESS"
  local summary="passed"
  if [[ $exit_code -ne 0 ]]; then
    status="FAILED"
    summary="failed (exit code ${exit_code})"
    FAILED_COUNT=$((FAILED_COUNT + 1))
  fi

  # 追加到 JSON 结果
  cat >> "${RESULT_FILE}.tmp" <<JSON
  {
    "name": "${name}",
    "category": "${category}",
    "status": "${status}",
    "summary": "${summary}",
    "duration_ms": $(( (end_time - start_time) * 1000 )),
    "start_time": "${start_time}",
    "end_time": "${end_time}"
  },
JSON
}

finish_result() {
  # 移除末尾逗号，闭合 JSON
  sed -i '$ s/,$//' "${RESULT_FILE}.tmp"
  echo ']' >> "${RESULT_FILE}.tmp"
  mv "${RESULT_FILE}.tmp" "${RESULT_FILE}"
}

# ---- 主流程 ---------------------------------------------------------
main() {
  if [[ -z "${GERRIT_REF}" ]]; then
    echo "错误: 需要 --ref 参数"
    exit 1
  fi

  echo "=== Gerrit CI ==="
  echo "Project : ${PROJECT}"
  echo "Ref     : ${GERRIT_REF}"
  echo "Host    : ${GERRIT_HOST}:${GERRIT_PORT}"

  # 准备构建目录
  rm -rf "${BUILD_DIR}"
  mkdir -p "${BUILD_DIR}"

  # 拉取代码：基于最新目标分支 + cherry-pick change
  echo ""
  echo ">>> 拉取代码: branch=${BRANCH} ref=${GERRIT_REF}"
  git init "${BUILD_DIR}/src"
  pushd "${BUILD_DIR}/src" > /dev/null
  git fetch "${GERRIT_URL}" "${BRANCH}" --depth=50
  git checkout -b ci-target FETCH_HEAD
  git fetch "${GERRIT_URL}" "${GERRIT_REF}" --depth=1
  local CHERRY_SHA
  CHERRY_SHA=$(git rev-parse FETCH_HEAD)
  if ! git cherry-pick --strategy-option=theirs "${CHERRY_SHA}"; then
    echo "WARNING: cherry-pick 冲突，使用 change 原始代码"
    git checkout "${CHERRY_SHA}"
  fi

  # 获取变更文件用于 clang-tidy diff 模式
  local BASE_SHA
  BASE_SHA=$(git rev-parse HEAD~1 2>/dev/null || echo "HEAD")
  local CHANGED_FILES
  CHANGED_FILES=$(git diff --name-only --diff-filter=ACMR "${BASE_SHA}" HEAD -- '*.cpp' '*.hpp' || true)

  popd > /dev/null
  local SRC_DIR="${BUILD_DIR}/src"

  # 初始化 JSON 结果
  echo '[' > "${RESULT_FILE}.tmp"

  # ---- 1. 编译 -------------------------------------------------------
  echo ""
  echo ">>> 编译"
  run_check "build" "build" bash -c "
    cmake -B '${BUILD_DIR}/build' '${SRC_DIR}' \
      -DCMAKE_BUILD_TYPE=Release \
      -DFLOWHUB_BUILD_TESTS=ON \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      && cmake --build '${BUILD_DIR}/build' --target flowhub flowhub_ut -j \$(nproc)
  "

  # ---- 2. 单元测试 ---------------------------------------------------
  echo ""
  echo ">>> 单元测试"
  run_check "unit-tests" "test" bash -c "
    cd '${BUILD_DIR}/build' && ctest --output-on-failure -L flowhub
  "

  # ---- 3. clang-format 检查 (仅变更文件) ----------------------------
  echo ""
  echo ">>> clang-format 检查"
  export CHANGED_FILES
  run_check "clang-format" "style" bash -c '
    if [[ -z "${CHANGED_FILES}" ]]; then
      echo "no changed C++ files"
      exit 0
    fi
    command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
    echo "${CHANGED_FILES}" | tr " " "\n" | while read -r f; do
      [[ -f "${f}" ]] || continue
      echo "  checking: ${f}"
      clang-format --dry-run --Werror "${f}" || true
    done
  '

  # ---- 4. clang-tidy -------------------------------------------------
  echo ""
  echo ">>> clang-tidy"
  echo "=== diagnostics ==="
  which clang-tidy-14 || echo "clang-tidy-14 NOT FOUND"
  clang-tidy-14 --version || echo "version check failed"
  echo "HeaderFilterRegex:"
  grep HeaderFilterRegex "${SRC_DIR}/.clang-tidy" || echo "NOT FOUND"
  echo "==================="
  if [[ -z "${CHANGED_FILES}" ]]; then
    run_check "clang-tidy" "static-analysis" bash -c "echo 'no changed C++ files'; exit 0"
  else
    run_check "clang-tidy" "static-analysis" bash -c "
      cd '${SRC_DIR}'
      echo '${CHANGED_FILES}' | tr ' ' '\n' | while read -r f; do
        [[ -f \"\${f}\" ]] || continue
        clang-tidy-14 --config-file='${SRC_DIR}/.clang-tidy' \
          -p='${BUILD_DIR}/build' \"\${f}\" 2>&1 || true
      done
    "
  fi

  # ---- 收尾 ----------------------------------------------------------
  finish_result

  echo ""
  echo "=== CI 完成 (${FAILED_COUNT} failed) ==="
  cat "${RESULT_FILE}"

  if [[ ${FAILED_COUNT} -gt 0 ]]; then
    exit 1
  fi
}

main
