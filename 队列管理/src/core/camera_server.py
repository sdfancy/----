"""
2D/3D 相机双通道服务端（支持单通道模式）。

职责：
1. 软件作为 TCP Server，分别监听 2D（必选）和 3D（可选）相机连接。
2. 对外提供按通道发送命令能力。
3. 接收相机报文后通过回调上抛业务层处理。

端口说明：
    - 2D 相机端口（默认 9001）：必选，接收 2D 相机数据
    - 3D 相机端口（默认 9002）：可选，通过 camera3d_enabled 控制
    
    花都现场只有一台 2D 相机：构造时传 camera3d_enabled=False 即可只监听 9001，
    避免 9002 端口无意义占用。

心跳处理：
    相机通常会定期发送心跳包维持 TCP 连接，本模块会过滤以下类型的心跳：
    - 空心跳（全 0x00 字节）
    - 换行/回车等无意义填充
    - 纯字母 H 心跳包
    
    心跳包不会传递给业务层，避免干扰正常数据处理。
"""

import logging
import socket
import threading
from dataclasses import dataclass
from typing import Callable, Dict, Optional

logger = logging.getLogger(__name__)


@dataclass
class CameraClientState:
    camera_key: str
    client_socket: Optional[socket.socket] = None
    address: Optional[tuple] = None
    connected: bool = False


class DualCameraServer:
    """2D/3D 相机双端口 TCP 服务（3D 通道可选）。"""

    # 心跳包最大长度阈值：短于此且全为零字节则视为心跳，直接忽略
    HEARTBEAT_MAX_LEN = 4

    def __init__(
        self,
        host: str = "0.0.0.0",
        camera2d_port: int = 9001,
        camera3d_port: int = 9002,
        camera3d_enabled: bool = True,
    ):
        self.host = host
        self.camera2d_port = int(camera2d_port)
        self.camera3d_port = int(camera3d_port)
        self._camera3d_enabled = bool(camera3d_enabled)

        self._servers: Dict[str, Optional[socket.socket]] = {"2d": None, "3d": None}
        self._accept_threads: Dict[str, Optional[threading.Thread]] = {"2d": None, "3d": None}
        self._recv_threads: Dict[str, Optional[threading.Thread]] = {"2d": None, "3d": None}
        self._clients: Dict[str, CameraClientState] = {
            "2d": CameraClientState(camera_key="2d"),
            "3d": CameraClientState(camera_key="3d"),
        }
        self._lock = threading.Lock()
        self.running = False
        self.data_callback: Optional[Callable[[str, bytes], None]] = None

    def set_data_callback(self, callback: Callable[[str, bytes], None]):
        self.data_callback = callback

    def start(self) -> bool:
        if self.running:
            return True
        try:
            self.running = True
            if not self._start_server("2d", self.camera2d_port):
                self.stop()
                return False
            if self._camera3d_enabled:
                if not self._start_server("3d", self.camera3d_port):
                    self.stop()
                    return False
                logger.info(
                    "[相机服务] 已启动 监听地址=%s 2D端口=%s 3D端口=%s",
                    self.host, self.camera2d_port, self.camera3d_port,
                )
            else:
                logger.info(
                    "[相机服务] 已启动 监听地址=%s 2D端口=%s（3D已禁用）",
                    self.host, self.camera2d_port,
                )
            return True
        except Exception as exc:
            logger.error("[相机服务] 启动失败：%s", exc)
            self.stop()
            return False

    def _start_server(self, camera_key: str, port: int) -> bool:
        try:
            server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((self.host, port))
            server.listen(5)
            self._servers[camera_key] = server

            thread = threading.Thread(
                target=self._accept_loop,
                args=(camera_key,),
                daemon=True,
                name=f"camera-accept-{camera_key}",
            )
            self._accept_threads[camera_key] = thread
            thread.start()
            return True
        except Exception as exc:
            logger.error("[相机服务] 通道=%s 启动失败：%s", camera_key, exc)
            return False

    def _accept_loop(self, camera_key: str):
        server = self._servers.get(camera_key)
        if server is None:
            return

        while self.running:
            try:
                client_socket, address = server.accept()
            except Exception as exc:
                if self.running:
                    logger.warning("[相机服务] 接收连接异常 通道=%s 错误=%s", camera_key, exc)
                continue

            with self._lock:
                state = self._clients[camera_key]
                old_socket = state.client_socket
                state.client_socket = client_socket
                state.address = address
                state.connected = True

            if old_socket is not None and old_socket is not client_socket:
                try:
                    old_socket.close()
                except Exception:
                    pass

            logger.info("[相机服务] 相机已连接 通道=%s 来源=%s:%s", camera_key, address[0], address[1])
            self._start_recv_thread(camera_key)

    def _start_recv_thread(self, camera_key: str):
        old_thread = self._recv_threads.get(camera_key)
        if old_thread and old_thread.is_alive():
            return

        thread = threading.Thread(
            target=self._recv_loop,
            args=(camera_key,),
            daemon=True,
            name=f"camera-recv-{camera_key}",
        )
        self._recv_threads[camera_key] = thread
        thread.start()

    @classmethod
    def _is_heartbeat(cls, data: bytes) -> bool:
        """
        判断是否为相机心跳包，如果是的直接忽略不上报业务层。
        
        心跳包过滤规则：
            1. 空数据或全 0x00 字节（常见空心跳）
            2. 仅包含换行/回车（\r\n 等无意义填充）
            3. 纯字母 H 心跳包（常见的心跳字符）
            
        Args:
            data: 接收到的原始数据
            
        Returns:
            bool: True 表示是心跳包，应忽略；False 表示是正常数据，应处理
        """
        if not data:
            return True
        if len(data) <= cls.HEARTBEAT_MAX_LEN and all(b == 0 for b in data):
            return True
        stripped = data.strip(b"\r\n\x00 ")
        if not stripped:
            return True
        # 过滤纯字母 H 心跳包
        if stripped.upper() == b"H" and len(stripped) == 1:
            return True
        return False

    def _recv_loop(self, camera_key: str):
        while self.running:
            with self._lock:
                state = self._clients[camera_key]
                sock = state.client_socket
            if sock is None or not state.connected:
                return

            try:
                data = sock.recv(4096)
                if not data:
                    self._mark_disconnected(camera_key)
                    return
                # 心跳包直接丢弃，不传给业务层
                if self._is_heartbeat(data):
                    logger.debug("[相机服务] 忽略心跳/噪声 通道=%s 字节数=%s", camera_key, len(data))
                    continue
                if self.data_callback:
                    self.data_callback(camera_key, data)
            except Exception:
                self._mark_disconnected(camera_key)
                return

    def _mark_disconnected(self, camera_key: str):
        """
        标记相机连接断开，并关闭对应的 socket。
        
        处理流程：
            1. 检查连接状态（避免重复处理）
            2. 清除客户端状态（socket、地址、连接标志）
            3. 关闭 socket（忽略可能的异常）
            4. 如果之前是连接状态，记录告警日志
            
        Args:
            camera_key: 相机标识（"2d" 或 "3d"）
        """
        with self._lock:
            state = self._clients[camera_key]
            sock = state.client_socket
            was_connected = state.connected
            state.client_socket = None
            state.address = None
            state.connected = False
        if sock:
            try:
                sock.close()
            except Exception:
                pass
        if was_connected:
            logger.warning("[相机服务] 相机已断开 通道=%s", camera_key)

    def send_to_camera(self, camera_key: str, payload: bytes) -> bool:
        """
        向指定相机发送数据。
        
        Args:
            camera_key: 相机标识（"2d" 或 "3d"）
            payload: 要发送的数据字节
            
        Returns:
            bool: True 表示发送成功，False 表示失败（未连接或发送异常）
        """
        if not payload:
            return False
        with self._lock:
            state = self._clients.get(camera_key)
            sock = state.client_socket if state else None
            connected = state.connected if state else False
            client_addr = state.address if state else None
        if not connected or sock is None:
            logger.warning("[相机服务] 相机未连接 通道=%s", camera_key)
            return False

        try:
            sock.sendall(payload)
            logger.info("[相机服务] 已发送到相机 通道=%s 地址=%s 数据=%s", camera_key, client_addr, payload.hex())
            return True
        except Exception as exc:
            logger.warning("[相机服务] 发送失败 通道=%s 错误=%s", camera_key, exc)
            self._mark_disconnected(camera_key)
            return False

    def get_status(self) -> Dict[str, Dict[str, object]]:
        with self._lock:
            result: Dict[str, Dict[str, object]] = {}
            for key, state in self._clients.items():
                # 未启用的 3D 通道不参与状态上报，避免 HMI 显示无意义的"未连接"
                if key == "3d" and not self._camera3d_enabled:
                    continue
                result[key] = {
                    "connected": state.connected,
                    "client_addr": state.address,
                }
            return result

    def stop(self):
        self.running = False
        for key in ("2d", "3d"):
            self._mark_disconnected(key)

        for key in ("2d", "3d"):
            server = self._servers.get(key)
            if server:
                try:
                    server.close()
                except Exception:
                    pass
                self._servers[key] = None
