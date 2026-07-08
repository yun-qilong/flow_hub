#!/usr/bin/env bash
# 启动 Gerrit CI Watcher 后台运行

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="/tmp/gerrit-ci-watcher.pid"
LOG_FILE="/tmp/gerrit-ci-watcher.log"

case "${1:-start}" in
  start)
    if [[ -f "${PID_FILE}" ]] && kill -0 "$(cat "${PID_FILE}")" 2>/dev/null; then
      echo "watcher 已在运行 (pid=$(cat "${PID_FILE}"))"
      exit 0
    fi
    echo "启动 watcher..."
    nohup python3 "${SCRIPT_DIR}/ci-watcher.py" --project flow_hub \
      >> "${LOG_FILE}" 2>&1 &
    echo $! > "${PID_FILE}"
    echo "已启动 (pid=$!)  日志: ${LOG_FILE}"
    ;;
  stop)
    if [[ -f "${PID_FILE}" ]]; then
      kill "$(cat "${PID_FILE}")" 2>/dev/null && echo "已停止" || echo "进程不存在"
      rm -f "${PID_FILE}"
    else
      echo "watcher 未在运行"
    fi
    ;;
  status)
    if [[ -f "${PID_FILE}" ]] && kill -0 "$(cat "${PID_FILE}")" 2>/dev/null; then
      echo "运行中 (pid=$(cat "${PID_FILE}"))"
      echo "最近日志:"
      tail -5 "${LOG_FILE}" 2>/dev/null
    else
      echo "未运行"
    fi
    ;;
  log)
    tail -f "${LOG_FILE}"
    ;;
  *)
    echo "用法: $0 {start|stop|status|log}"
    exit 1
    ;;
esac
