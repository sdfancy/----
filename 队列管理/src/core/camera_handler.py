"""
相机通信模块（异步接收）。

职责：
1. 维护到相机的 TCP 连接。
2. 异步接收相机数据并入本地队列。
3. 支持回调方式通知上层处理。

使用说明：
    1. 创建 AsyncCameraHandler 实例，指定相机 IP 和端口
    2. 设置数据回调 set_data_callback
    3. 调用 connect() 连接相机
    4. 调用 start_receiving() 启动后台接收线程
    5. 可以通过 get_latest_data() 获取最新数据，或等待回调通知
    6. 使用完后调用 stop() 停止接收并断开连接
"""

import queue
import socket
import threading
import time
from dataclasses import dataclass
from typing import Callable, Optional


@dataclass
class CameraData:
    """相机数据结构。"""

    raw_data: bytes
    timestamp: float
    sensor_count: int = 0
    processed: bool = False


class AsyncCameraHandler:
    """异步相机处理器。"""

    def __init__(self, host: str = "192.168.1.101", port: int = 8080):
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.connected = False
        self.data_queue = queue.Queue()
        self.receiving = False
        self.receive_thread: Optional[threading.Thread] = None
        self.data_callback: Optional[Callable[[CameraData], None]] = None
        self.reconnect_interval = 2.0
        self._stop_event = threading.Event()

    def set_data_callback(self, callback: Callable[[CameraData], None]):
        """设置数据回调（收到相机数据后触发）。"""
        self.data_callback = callback

    def connect(self) -> bool:
        """连接相机 TCP 服务。"""
        try:
            if self.socket:
                try:
                    self.socket.close()
                except Exception:
                    pass
                self.socket = None

            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            self.socket.settimeout(3.0)
            self.socket.connect((self.host, self.port))
            self.socket.settimeout(None)
            self.connected = True
            print(f"[CAMERA] 成功连接到 {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"[CAMERA] 连接失败: {e}")
            self.connected = False
            return False

    def start_receiving(self) -> bool:
        """启动后台接收线程。"""
        if not self.connected:
            print("[CAMERA] 未连接，无法启动接收")
            return False

        if self.receive_thread and self.receive_thread.is_alive():
            return True

        self._stop_event.clear()
        self.receiving = True
        self.receive_thread = threading.Thread(
            target=self._receive_loop,
            daemon=True,
            name="CameraReceiver",
        )
        self.receive_thread.start()
        print("[CAMERA] 异步接收已启动")
        return True

    def _receive_loop(self):
        """
        循环接收相机数据并投递到队列/回调。
        
        处理流程：
            1. 检查连接状态，如果已断开则触发重连
            2. 使用短超时（1秒）接收数据，避免线程阻塞
            3. 收到数据后：
               - 封装为 CameraData 对象
               - 放入本地队列（供其他模块主动获取）
               - 触发回调通知上层
            4. 如果连接断开，记录日志并触发重连
            
        注意：
            - 使用短超时是为了能快速响应 stop() 命令
            - 数据会同时放入队列和触发回调，两者互不影响
        """
        print("[CAMERA] 开始监听数据...")

        while self.receiving and (not self._stop_event.is_set()):
            if not self.connected:
                # 若连接状态已失效（例如发送失败标记断链），在接收线程内负责拉起重连。
                self._handle_disconnect()
                continue

            try:
                # 用短超时避免线程永久阻塞，便于 stop 时快速退出。
                self.socket.settimeout(1.0)
                data = self.socket.recv(1024)

                if data:
                    camera_data = CameraData(
                        raw_data=data,
                        timestamp=time.time(),
                    )
                    self.data_queue.put(camera_data)
                    if self.data_callback:
                        self.data_callback(camera_data)
                    print(f"[CAMERA] 接收到 {len(data)} 字节数据")
                else:
                    # 对端主动断开
                    print("[CAMERA] 连接被相机关闭")
                    self._handle_disconnect()
                    continue

            except socket.timeout:
                # 正常超时继续循环
                continue
            except Exception as e:
                print(f"[CAMERA] 接收过程中出错: {e}")
                self._handle_disconnect()
                continue

    def trigger_capture(
        self,
        command: int = 0x01,
        sensor_count: Optional[int] = None,
        concurrent_mode: bool = False,
    ) -> bool:
        """
        发送拍照触发命令到相机。

        编码规则：
            1) 非并发模式：16位整数二进制帧
               - 仅命令：command(2字节, 大端序)
               - 当传入 sensor_count 时：command(2字节)+count(2字节)
            2) 并发模式：ASCII 文本帧
               - 格式：CMD:01 或 CMD:01,COUNT:123

        Args:
            command: 触发命令码（如 0x01, 0x11, 0x12）
            sensor_count: 传感器计数值（可选，用于关联）
            concurrent_mode: 是否使用并发模式（ASCII编码）

        Returns:
            bool: True 表示发送成功，False 表示失败
        """
        if not self.connected:
            print("[CAMERA] 未连接，无法触发拍照")
            return False

        try:
            if concurrent_mode:
                if sensor_count is None:
                    payload = f"CMD:{int(command):02X}".encode("ascii")
                else:
                    payload = f"CMD:{int(command):02X},COUNT:{int(sensor_count)}".encode("ascii")
            else:
                if sensor_count is None:
                    payload = int(command).to_bytes(2, byteorder="big", signed=False)
                else:
                    payload = (
                        int(command).to_bytes(2, byteorder="big", signed=False)
                        + int(sensor_count).to_bytes(2, byteorder="big", signed=False)
                    )
            self.socket.send(payload)
            print("[CAMERA] 拍照触发命令已发送")
            return True
        except Exception as e:
            print(f"[CAMERA] 触发命令发送失败: {e}")
            # 仅标记断链并关闭 socket，重连由接收线程统一处理。
            self.connected = False
            if self.socket:
                try:
                    self.socket.close()
                except Exception:
                    pass
                self.socket = None
            return False

    def get_latest_data(self, timeout: float = 0.1) -> Optional[CameraData]:
        """从本地队列取最近一条相机数据。"""
        try:
            data = self.data_queue.get(timeout=timeout)
            print(f"[CAMERA] 获取到数据: {len(data.raw_data)} 字节")
            return data
        except queue.Empty:
            return None

    def _handle_disconnect(self):
        """处理断链并持续重连，直到成功或 stop。"""
        print("[CAMERA] 处理连接断开...")
        self.connected = False
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
            self.socket = None

        while not self._stop_event.is_set():
            print("[CAMERA] 尝试重新连接...")
            time.sleep(self.reconnect_interval)

            if self.connect():
                print("[CAMERA] 重连成功")
                self.start_receiving()
                return
            print("[CAMERA] 重连失败，等待下一次重试")

    def get_queue_size(self) -> int:
        """获取待处理相机数据数量。"""
        return self.data_queue.qsize()

    def stop(self):
        """停止接收并释放连接。"""
        print("[CAMERA] 正在停止相机处理器...")
        self._stop_event.set()
        self.receiving = False
        self.connected = False

        if self.receive_thread and self.receive_thread.is_alive():
            self.receive_thread.join(timeout=2)

        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
        print("[CAMERA] 相机处理器已停止")
