#!/usr/bin/env bash
# 统一启动/停止/查看 CI 服务 (Gerrit + Jenkins + issue-mirror + watcher)
# 端口: Gerrit HTTP=18080, Gerrit SSH=19418, Jenkins=18090, issue-mirror=18091

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GERRIT_SH="/opt/gerrit/bin/gerrit.sh"
JENKINS_WAR="/opt/jenkins/jenkins.war"
JENKINS_HOME="/opt/jenkins/home"
JENKINS_HTTP_PORT="18090"
JENKINS_LOG="/opt/jenkins/jenkins.log"
MIRROR_SCRIPT="${SCRIPT_DIR}/issue-mirror/server.py"
WATCHER_SCRIPT="${SCRIPT_DIR}/gerrit-event-watcher.py"
PID_DIR="/tmp/ci-services"
JENKINS_PID_FILE="${PID_DIR}/jenkins.pid"
MIRROR_PID_FILE="${PID_DIR}/mirror.pid"
WATCHER_PID_FILE="${PID_DIR}/watcher.pid"

mkdir -p "${PID_DIR}"

pid_alive()
{
  local pid_file="$1"
  if [[ -f "${pid_file}" ]] && kill -0 "$(cat "${pid_file}")" 2>/dev/null; then
    return 0
  fi
  return 1
}

start_gerrit()
{
  if pgrep -f GerritCodeReview >/dev/null 2>&1; then
    echo "  Gerrit 已在运行"
    return
  fi
  echo "  启动 Gerrit (HTTP:18080, SSH:19418)..."
  "${GERRIT_SH}" start
}

start_jenkins()
{
  if pgrep -f "jenkins.war" >/dev/null 2>&1; then
    echo "  Jenkins 已在运行"
    return
  fi
  echo "  启动 Jenkins (端口 ${JENKINS_HTTP_PORT}, JENKINS_HOME=${JENKINS_HOME})..."
  JENKINS_HOME="${JENKINS_HOME}" nohup java -jar "${JENKINS_WAR}" \
    --httpPort="${JENKINS_HTTP_PORT}" >> "${JENKINS_LOG}" 2>&1 &
  echo $! > "${JENKINS_PID_FILE}"
}

start_mirror()
{
  if pid_alive "${MIRROR_PID_FILE}"; then
    echo "  issue-mirror 已在运行"
    return
  fi
  echo "  启动 issue-mirror (端口 18091)..."
  nohup python3 "${MIRROR_SCRIPT}" >> /tmp/issue-mirror.log 2>&1 &
  echo $! > "${MIRROR_PID_FILE}"
}

start_watcher()
{
  if pid_alive "${WATCHER_PID_FILE}"; then
    echo "  gerrit-event-watcher 已在运行"
    return
  fi
  echo "  启动 gerrit-event-watcher..."
  nohup python3 -u "${WATCHER_SCRIPT}" >> /tmp/gerrit-event-watcher.log 2>&1 &
  echo $! > "${WATCHER_PID_FILE}"
}

stop_all()
{
  echo "停止服务..."
  pkill -f GerritCodeReview 2>/dev/null && echo "  Gerrit 已停止" || echo "  Gerrit 未运行"
  pkill -f "jenkins.war" 2>/dev/null && echo "  Jenkins 已停止" || echo "  Jenkins 未运行"
  pkill -f "issue-mirror/server.py" 2>/dev/null && echo "  issue-mirror 已停止" || echo "  issue-mirror 未运行"
  pkill -f "gerrit-event-watcher.py" 2>/dev/null && echo "  watcher 已停止" || echo "  watcher 未运行"
  rm -f "${JENKINS_PID_FILE}" "${MIRROR_PID_FILE}" "${WATCHER_PID_FILE}"
}

show_status()
{
  echo "=== CI 服务状态 ==="
  if pgrep -f GerritCodeReview >/dev/null 2>&1; then
    echo "  Gerrit:     运行中 (http://localhost:18080, ssh:19418)"
  else
    echo "  Gerrit:     未运行"
  fi
  if pgrep -f "jenkins.war" >/dev/null 2>&1; then
    echo "  Jenkins:    运行中 (http://localhost:18090)"
  else
    echo "  Jenkins:    未运行"
  fi
  if pid_alive "${MIRROR_PID_FILE}"; then
    echo "  issue-mirror: 运行中 (http://localhost:18091)"
  else
    echo "  issue-mirror: 未运行"
  fi
  if pid_alive "${WATCHER_PID_FILE}"; then
    echo "  watcher:    运行中"
  else
    echo "  watcher:    未运行"
  fi
}

case "${1:-start}" in
  start)
    echo "启动 CI 服务..."
    start_gerrit
    start_jenkins
    start_mirror
    start_watcher
    echo "完成。"
    ;;
  stop)
    stop_all
    ;;
  status)
    show_status
    ;;
  *)
    echo "用法: $0 {start|stop|status}"
    exit 1
    ;;
esac
