#!/usr/bin/env bash
# 配置 80 端口导航服务:访问 http://localhost/ 显示所有 CI 服务入口
# 需要 root 权限(80 端口 < 1024): sudo bash scripts/setup-port80.sh start

set -euo pipefail

NAV_SCRIPT="/opt/ci-port80/nav-server.py"
PID_FILE="/tmp/ci-port80.pid"
LOG_FILE="/tmp/ci-port80.log"

require_root()
{
  if [[ "$(id -u)" -ne 0 ]]; then
    echo "错误:需要 root 权限,请用: sudo bash $0 $*"
    exit 1
  fi
}

write_server()
{
  mkdir -p /opt/ci-port80
  cat > "${NAV_SCRIPT}" << 'PYEOF'
#!/usr/bin/env python3
import http.server

PAGE = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>FlowHub CI 服务导航</title>
<style>
body{font-family:sans-serif;max-width:600px;margin:40px auto;padding:0 20px}
a{display:block;padding:12px;margin:8px 0;background:#f5f5f5;border-radius:6px;text-decoration:none;color:#333;font-size:18px}
a:hover{background:#e8e8e8}
h1{font-size:24px}
</style></head>
<body><h1>FlowHub CI 服务</h1>
<a href="http://localhost:18090">Jenkins (http://localhost:18090)</a>
<a href="http://localhost:18080">Gerrit (http://localhost:18080)</a>
<a href="http://localhost:18091">issue-mirror (http://localhost:18091)</a>
</body></html>"""

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(PAGE.encode())

    def log_message(self, *args):
        pass

if __name__ == "__main__":
    server = http.server.HTTPServer(("0.0.0.0", 80), Handler)
    server.serve_forever()
PYEOF
}

start()
{
  require_root
  if [[ -f "${PID_FILE}" ]] && kill -0 "$(cat "${PID_FILE}")" 2>/dev/null; then
    echo "80 端口导航服务已在运行: http://localhost/"
    return
  fi
  write_server
  nohup python3 "${NAV_SCRIPT}" >> "${LOG_FILE}" 2>&1 &
  echo $! > "${PID_FILE}"
  echo "80 端口导航服务已启动: http://localhost/"
}

stop()
{
  if [[ -f "${PID_FILE}" ]] && kill -0 "$(cat "${PID_FILE}")" 2>/dev/null; then
    kill "$(cat "${PID_FILE}")" 2>/dev/null && rm -f "${PID_FILE}"
    echo "已停止"
  else
    echo "未在运行"
  fi
}

status()
{
  if [[ -f "${PID_FILE}" ]] && kill -0 "$(cat "${PID_FILE}")" 2>/dev/null; then
    echo "运行中: http://localhost/"
  else
    echo "未运行"
  fi
}

case "${1:-start}" in
  start) start ;;
  stop) stop ;;
  status) status ;;
  *) echo "用法: sudo bash $0 {start|stop|status}"; exit 1 ;;
esac
