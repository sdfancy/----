"""
PLC 双端口处理器（增强版）。

通道说明：
1. enqueue 通道（默认 9999）：接收 PLC 入队报文。
2. dequeue 通道（默认 9009）：接收 PLC 出队指针，同时承载软件->PLC 的反馈码发送。

原始通讯记录：
    在详细日志模式（detailed）下，会记录通讯的原始字节内容。
    调用 get_enqueue_logger() 和 get_dequeue_logger() 获取专用日志器。
"""

import collections
import logging
import socket
import threading
import time
from dataclasses import dataclass
from typing import Any, Callable, Deque, Dict, List, Optional

from src.utils.logger import get_enqueue_logger, get_dequeue_logger, log_raw_data

logger = logging.getLogger(__name__)


@dataclass
class PLCConnectionInfo:
    """单个 TCP 连接信息。"""

    connection_type: str  # "enqueue" 或 "dequeue"
    socket: socket.socket
    address: tuple
    connected: bool = True


class DualPortPLCHandler:
    """PLC 双端口 TCP 服务。"""

    def __init__(self, host: str = "0.0.0.0", enqueue_port: int = 9999, dequeue_port: int = 9009):
        self.host = host
        self.enqueue_port = enqueue_port
        self.dequeue_port = dequeue_port

        self.enqueue_server: Optional[socket.socket] = None
        self.dequeue_server: Optional[socket.socket] = None

        self.running = False
        self.connections: Dict[str, PLCConnectionInfo] = {}
        self._conn_lock = threading.Lock()

        # 回调由主控模块注入：
        # enqueue_cb(bytes)：处理入队报文
        # dequeue_cb(bytes)：处理出队指针报文
        self.enqueue_callback: Optional[Callable[[bytes], None]] = None
        self.dequeue_callback: Optional[Callable[[bytes], None]] = None

        self.server_threads: Dict[str, threading.Thread] = {}
        self.connection_threads: Dict[str, threading.Thread] = {}

        # 最近一次出队指针，用于检测变化
        self.last_dequeue_pointers: Dict[int, int] = {}

        # PLC 命令历史记录（供 HMI 展示）
        # 每条记录：{"ts": float, "channel": "enqueue"/"dequeue", "raw_hex": str, "display": str}
        self._cmd_history: Deque[Dict[str, Any]] = collections.deque(maxlen=100)
        self._cmd_history_lock = threading.Lock()

        logger.info(
            "[PLC] 双端口处理器已初始化 监听地址=%s 入队端口=%s 出队端口=%s",
            self.host,
            self.enqueue_port,
            self.dequeue_port,
        )

    def set_callbacks(
        self,
        enqueue_cb: Optional[Callable[[bytes], None]] = None,
        dequeue_cb: Optional[Callable[[bytes], None]] = None,
    ):
        """设置入队/出队回调。"""
        self.enqueue_callback = enqueue_cb
        self.dequeue_callback = dequeue_cb

    def start_servers(self) -> bool:
        """启动双端口服务。"""
        try:
            # 先置运行态，再起线程，避免 accept 线程启动瞬间读取到 False。
            self.running = True

            if not self._start_enqueue_server():
                self.running = False
                return False

            if not self._start_dequeue_server():
                self._stop_enqueue_server()
                self.running = False
                return False

            logger.info("[PLC] 双端口服务已启动")
            return True
        except Exception as exc:
            self.running = False
            logger.error("[PLC] 双端口服务启动失败：%s", exc)
            return False

    def _start_enqueue_server(self) -> bool:
        """启动入队监听端口。"""
        try:
            self.enqueue_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.enqueue_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.enqueue_server.bind((self.host, self.enqueue_port))
            self.enqueue_server.listen(5)

            self.server_threads["enqueue"] = threading.Thread(
                target=self._enqueue_accept_loop,
                daemon=True,
                name="plc-enqueue-accept",
            )
            self.server_threads["enqueue"].start()
            return True
        except Exception as exc:
            logger.error("[PLC] 入队端口启动失败：%s", exc)
            return False

    def _start_dequeue_server(self) -> bool:
        """启动出队监听端口。"""
        try:
            self.dequeue_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.dequeue_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.dequeue_server.bind((self.host, self.dequeue_port))
            self.dequeue_server.listen(5)

            self.server_threads["dequeue"] = threading.Thread(
                target=self._dequeue_accept_loop,
                daemon=True,
                name="plc-dequeue-accept",
            )
            self.server_threads["dequeue"].start()
            return True
        except Exception as exc:
            logger.error("[PLC] 出队端口启动失败：%s", exc)
            return False

    def _enqueue_accept_loop(self):
        """持续接收入队连接。"""
        while self.running:
            try:
                client_socket, address = self.enqueue_server.accept()
                conn_id = f"enqueue_{address[0]}_{address[1]}"
                conn_info = PLCConnectionInfo("enqueue", client_socket, address)

                with self._conn_lock:
                    self.connections[conn_id] = conn_info

                thread = threading.Thread(
                    target=self._handle_enqueue_connection,
                    args=(conn_id,),
                    daemon=True,
                    name=f"plc-enqueue-{address[0]}-{address[1]}",
                )
                self.connection_threads[conn_id] = thread
                thread.start()
            except Exception as exc:
                if self.running:
                    logger.error("[PLC] 入队连接接收异常：%s", exc)

    def _dequeue_accept_loop(self):
        """持续接收出队连接。"""
        while self.running:
            try:
                client_socket, address = self.dequeue_server.accept()
                conn_id = f"dequeue_{address[0]}_{address[1]}"
                conn_info = PLCConnectionInfo("dequeue", client_socket, address)

                with self._conn_lock:
                    self.connections[conn_id] = conn_info

                thread = threading.Thread(
                    target=self._handle_dequeue_connection,
                    args=(conn_id,),
                    daemon=True,
                    name=f"plc-dequeue-{address[0]}-{address[1]}",
                )
                self.connection_threads[conn_id] = thread
                thread.start()
            except Exception as exc:
                if self.running:
                    logger.error("[PLC] 出队连接接收异常：%s", exc)

    def _record_cmd_history(self, channel: str, data: bytes):
        """记录收到的 PLC 命令到历史缓冲区（供 HMI 展示）。"""
        raw_hex = data.hex(" ").upper() if data else ""
        display = self._describe_plc_frame(channel, data)
        entry: Dict[str, Any] = {
            "ts": time.time(),
            "channel": channel,
            "raw_hex": raw_hex,
            "display": display,
        }
        with self._cmd_history_lock:
            self._cmd_history.append(entry)

    @staticmethod
    def _describe_plc_frame(channel: str, data: bytes) -> str:
        """将原始字节描述为可读字符串，方便 HMI 显示。"""
        if not data:
            return "(空)"
        if channel == "enqueue" and len(data) >= 6:
            cmd = int.from_bytes(data[:2], "big")
            count = int.from_bytes(data[2:4], "big")
            ptr = int.from_bytes(data[4:6], "big")
            cmd_name = {1: "01-开始入队", 0x11: "11-触发相机2", 0x12: "12-入队完成"}.get(cmd, f"CMD={cmd}")
            return f"[入队] {cmd_name} count={count} ptr={ptr}"
        if channel == "dequeue" and len(data) == 4:
            p1 = int.from_bytes(data[:2], "big")
            p2 = int.from_bytes(data[2:], "big")
            return f"[出队] arm1_ptr={p1} arm2_ptr={p2}"
        return f"[{channel}] hex={data.hex(' ').upper()}"

    def _handle_enqueue_connection(self, conn_id: str):
        """处理单条入队连接的数据接收。"""
        # 获取入队专用日志器（详细模式下会写入 enqueue.log）
        enqueue_logger = get_enqueue_logger()
        
        conn = self._get_connection(conn_id)
        if not conn:
            return

        try:
            while self.running and conn.connected:
                # 入队帧格式：command(2) + count(2) + pointer(2) => 6字节
                data = self._recv_enqueue_frame(conn.socket)
                if not data:
                    break
                
                # 记录原始通讯内容（详细日志模式下）
                client_addr = f"{conn.address[0]}:{conn.address[1]}"
                log_raw_data(enqueue_logger, "RECV", client_addr, data.hex().upper(), "入队请求")
                
                self._record_cmd_history("enqueue", data)
                if self.enqueue_callback:
                    self.enqueue_callback(data)
        except Exception as exc:
            logger.error("[PLC] 入队连接处理异常：%s", exc)
        finally:
            self._close_connection(conn_id)

    def _handle_dequeue_connection(self, conn_id: str):
        """处理单条出队连接的数据接收。"""
        # 获取出队专用日志器（详细模式下会写入 dequeue.log）
        dequeue_logger = get_dequeue_logger()
        
        conn = self._get_connection(conn_id)
        if not conn:
            return

        try:
            while self.running and conn.connected:
                # 出队报文固定 4 字节，使用精确读取防止粘包/半包导致长度不足。
                data = self._recv_exact(conn.socket, 4)
                if not data:
                    break

                # 记录原始通讯内容（详细日志模式下）
                client_addr = f"{conn.address[0]}:{conn.address[1]}"
                log_raw_data(dequeue_logger, "RECV", client_addr, data.hex().upper(), "出队请求")
                
                self._record_cmd_history("dequeue", data)
                changed = self._parse_and_detect_dequeue_changes(data)
                if changed and self.dequeue_callback:
                    self.dequeue_callback(data)
        except Exception as exc:
            logger.error("[PLC] 出队连接处理异常：%s", exc)
        finally:
            self._close_connection(conn_id)

    def _recv_exact(self, sock: socket.socket, size: int) -> bytes:
        """精确读取指定字节数。"""
        chunks = []
        received = 0
        while received < size:
            chunk = sock.recv(size - received)
            if not chunk:
                return b""
            chunks.append(chunk)
            received += len(chunk)
        return b"".join(chunks)

    def _recv_enqueue_frame(self, sock: socket.socket) -> bytes:
        """
        读取一帧 PLC 入队数据（6字节格式）。

        格式：command(2) + count(2) + pointer(2) = 6字节
        """
        data = self._recv_exact(sock, 6)
        return data

    def _parse_and_detect_dequeue_changes(self, data: bytes) -> Dict[int, int]:
        """解析出队指针并返回发生变化的项。"""
        if len(data) != 4:
            return {}

        arm1_pointer = int.from_bytes(data[:2], byteorder="big")
        arm2_pointer = int.from_bytes(data[2:], byteorder="big")

        changed: Dict[int, int] = {}

        if arm1_pointer != self.last_dequeue_pointers.get(1, 0):
            self.last_dequeue_pointers[1] = arm1_pointer
            changed[1] = arm1_pointer

        if arm2_pointer != self.last_dequeue_pointers.get(2, 0):
            self.last_dequeue_pointers[2] = arm2_pointer
            changed[2] = arm2_pointer

        return changed

    def send_dequeue_feedback(self, feedback_code: str) -> bool:
        """
        通过 dequeue 通道发送反馈码给 PLC。

        反馈格式：2字符
        - 1N / 1D
        - 2N / 2D
        """
        # 获取出队专用日志器（详细模式下会写入 dequeue.log）
        dequeue_logger = get_dequeue_logger()
        
        if len(feedback_code) != 2:
            raise ValueError("PLC feedback code must be 2 characters")

        payload = feedback_code.encode("ascii")

        # 仅对 dequeue 连接发送反馈。
        targets = []
        with self._conn_lock:
            for conn in self.connections.values():
                if conn.connection_type == "dequeue" and conn.connected:
                    targets.append(conn)

        if not targets:
            logger.warning("[PLC] 没有可用出队连接，无法发送反馈码：%s", feedback_code)
            return False

        success = False
        sent_targets = []
        for conn in targets:
            try:
                conn.socket.sendall(payload)
                success = True
                target_addr = f"{conn.address[0]}:{conn.address[1]}"
                sent_targets.append(target_addr)
                
                # 记录发送的原始内容（详细日志模式下）
                log_raw_data(dequeue_logger, "SEND", target_addr, payload.hex().upper(), f"反馈码={feedback_code}")
            except Exception as exc:
                logger.warning("[PLC] 反馈码发送失败 目标=%s 错误=%s", conn.address, exc)

        if success:
            logger.info(
                "[PLC] 反馈码已发送：%s 目标=%s",
                feedback_code,
                ",".join(sent_targets),
            )

        return success

    def send_enqueue_feedback(self, message: str) -> bool:
        """
        通过 enqueue 通道发送阶段握手消息给 PLC（例如 "11" / "21"）。
        """
        if not message:
            return False

        payload = message.encode("ascii", errors="ignore")
        if not payload:
            return False

        targets = []
        with self._conn_lock:
            for conn in self.connections.values():
                if conn.connection_type == "enqueue" and conn.connected:
                    targets.append(conn)

        if not targets:
            logger.warning("[PLC] 没有可用入队连接，无法发送阶段消息：%s", message)
            return False

        success = False
        sent_targets = []
        for conn in targets:
            try:
                conn.socket.sendall(payload)
                success = True
                sent_targets.append(f"{conn.address[0]}:{conn.address[1]}")
            except Exception as exc:
                logger.warning("[PLC] 阶段消息发送失败 目标=%s 错误=%s", conn.address, exc)

        if success:
            logger.info(
                "[PLC] 阶段消息已发送：%s 目标=%s",
                message,
                ",".join(sent_targets),
            )
        return success

    def _get_connection(self, conn_id: str) -> Optional[PLCConnectionInfo]:
        """线程安全获取连接对象。"""
        with self._conn_lock:
            return self.connections.get(conn_id)

    def _close_connection(self, conn_id: str):
        """关闭并移除连接。"""
        with self._conn_lock:
            conn = self.connections.get(conn_id)
            if not conn:
                return
            conn.connected = False
            try:
                conn.socket.close()
            except Exception:
                pass
            del self.connections[conn_id]

    def _stop_enqueue_server(self):
        """关闭入队监听 socket。"""
        if self.enqueue_server:
            try:
                self.enqueue_server.close()
            except Exception:
                pass
            self.enqueue_server = None

    def _stop_dequeue_server(self):
        """关闭出队监听 socket。"""
        if self.dequeue_server:
            try:
                self.dequeue_server.close()
            except Exception:
                pass
            self.dequeue_server = None

    def stop(self):
        """停止服务并释放资源。"""
        self.running = False

        with self._conn_lock:
            conn_ids = list(self.connections.keys())

        for conn_id in conn_ids:
            self._close_connection(conn_id)

        self._stop_enqueue_server()
        self._stop_dequeue_server()

        for thread in self.connection_threads.values():
            if thread.is_alive():
                thread.join(timeout=1.0)

    def get_status(self) -> Dict[str, Any]:
        """返回状态快照（用于监控/HMI）。"""
        with self._conn_lock:
            active = len(self.connections)
            enqueue_active = 0
            dequeue_active = 0
            for conn in self.connections.values():
                if not conn.connected:
                    continue
                if conn.connection_type == "enqueue":
                    enqueue_active += 1
                elif conn.connection_type == "dequeue":
                    dequeue_active += 1

        with self._cmd_history_lock:
            cmd_history: List[Dict[str, Any]] = list(self._cmd_history)

        return {
            "running": self.running,
            "enqueue_port": self.enqueue_port,
            "dequeue_port": self.dequeue_port,
            "active_connections": active,
            "enqueue_connections": enqueue_active,
            "dequeue_connections": dequeue_active,
            "last_dequeue_pointers": self.last_dequeue_pointers.copy(),
            "has_enqueue_callback": self.enqueue_callback is not None,
            "has_dequeue_callback": self.dequeue_callback is not None,
            "cmd_history": cmd_history,
        }
