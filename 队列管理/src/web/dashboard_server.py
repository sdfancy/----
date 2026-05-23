"""
HMI 可视化服务（轻量 HTTP 版本）。

提供两个入口：
1. /           -> 返回 dashboard.html
2. /api/status -> 返回系统状态快照 JSON
"""

import json
import logging
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Callable, Dict, Optional
from urllib.parse import urlparse

logger = logging.getLogger(__name__)


class DashboardServer:
    """提供前端页面与状态 API 的轻量服务。"""

    def __init__(
        self,
        snapshot_provider: Callable[[], Dict],
        host: str = "127.0.0.1",
        port: int = 8090,
    ):
        # snapshot_provider 由主控注入，调用时返回当前系统快照
        self.snapshot_provider = snapshot_provider
        self.host = host
        self.port = port
        self._httpd: Optional[ThreadingHTTPServer] = None
        self._thread: Optional[threading.Thread] = None
        self.running = False

    def start(self) -> bool:
        """启动 HTTP 服务线程。"""
        if self.running:
            return True

        try:
            handler_cls = self._build_handler_class()
            self._httpd = ThreadingHTTPServer((self.host, self.port), handler_cls)
            self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True, name="dashboard-server")
            self._thread.start()
            self.running = True
            logger.info("[HMI] 页面服务已启动 http://%s:%s", self.host, self.port)
            return True
        except Exception as exc:
            logger.error("[HMI] 页面服务启动失败：%s", exc)
            self.running = False
            self._httpd = None
            self._thread = None
            return False

    def stop(self):
        """停止服务线程并关闭 socket。"""
        if not self.running:
            return

        self.running = False
        if self._httpd:
            try:
                self._httpd.shutdown()
                self._httpd.server_close()
            except Exception:
                pass

        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)

        self._httpd = None
        self._thread = None
        logger.info("[HMI] 页面服务已停止")

    @staticmethod
    def _resolve_static_dir() -> Path:
        """
        兼容 PyInstaller exe 和直接 python 两种运行方式。

        - 直接运行：__file__ 指向 src/web/dashboard_server.py，
          静态文件在 src/web/static/
        - PyInstaller onedir：文件被打包进 _MEIPASS，
          sys._MEIPASS/src/web/static/ 下有 dashboard.html
        """
        import sys
        if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
            # exe 模式：_MEIPASS 是 PyInstaller 展开数据文件的根目录
            return Path(sys._MEIPASS) / "src" / "web" / "static"
        # 开发模式：相对于本文件所在目录
        return Path(__file__).parent / "static"

    def _build_handler_class(self):
        """构建请求处理器类（闭包方式注入依赖）。"""
        snapshot_provider = self.snapshot_provider
        html_path = self._resolve_static_dir() / "dashboard.html"

        class DashboardHandler(BaseHTTPRequestHandler):
            def do_GET(self):
                path = urlparse(self.path).path
                if path in ("/", "/index.html"):
                    self._serve_index(html_path)
                    return
                if path == "/api/status":
                    self._serve_status(snapshot_provider)
                    return

                self.send_response(404)
                self.send_header("Content-Type", "text/plain; charset=utf-8")
                self.end_headers()
                self.wfile.write(b"Not Found")

            def _serve_index(self, file_path: Path):
                """返回前端页面。"""
                try:
                    html = file_path.read_text(encoding="utf-8")
                    payload = html.encode("utf-8")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)
                except Exception as exc:
                    self.send_response(500)
                    self.send_header("Content-Type", "text/plain; charset=utf-8")
                    self.end_headers()
                    self.wfile.write(f"Dashboard load error: {exc}".encode("utf-8"))

            def _serve_status(self, provider: Callable[[], Dict]):
                """返回实时状态 JSON。"""
                try:
                    payload_dict = provider()
                    payload = json.dumps(payload_dict, ensure_ascii=False).encode("utf-8")
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json; charset=utf-8")
                    self.send_header("Cache-Control", "no-store")
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)
                except Exception as exc:
                    self.send_response(500)
                    self.send_header("Content-Type", "application/json; charset=utf-8")
                    self.end_headers()
                    self.wfile.write(json.dumps({"error": str(exc)}).encode("utf-8"))

            def log_message(self, format, *args):
                # 默认 http.server 会打印到 stderr，这里统一接到项目 logger。
                logger.debug("[HMI] " + format, *args)

        return DashboardHandler
