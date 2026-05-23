"""
设备联调模拟器（PLC + 相机 + 机械臂）

用途：
1. 用于本地快速联调主程序，不依赖真实硬件。
2. 提供 PLC 三个按钮：
   - 传感器触发
   - 机械臂1出队
   - 机械臂2出队
3. 自动模拟：
   - PLC 双端口发送数据
   - 相机接收触发并回包
   - 机械臂发送 data/1 协议

运行前建议：
1. 先启动本模拟器（相机/机械臂服务端先就绪）。
2. 再启动主程序 src/main.py。
3. 回到模拟器点击“连接PLC到软件”。
"""

from __future__ import annotations

import queue
import re
import socket
import struct
import threading
import time
import tkinter as tk
from collections import deque
from dataclasses import dataclass
from tkinter import messagebox, ttk
from typing import Callable, Optional


# =========================
# 工具：线程安全日志输出
# =========================
class LogBus:
    """跨线程日志总线，后台线程把消息放入队列，主线程定时取出显示。"""

    def __init__(self):
        self._queue: "queue.Queue[str]" = queue.Queue()

    def push(self, msg: str):
        ts = time.strftime("%H:%M:%S")
        self._queue.put(f"[{ts}] {msg}")

    def drain(self):
        lines = []
        while True:
            try:
                lines.append(self._queue.get_nowait())
            except queue.Empty:
                break
        return lines


# =========================
# PLC 客户端模拟器
# =========================
class PLCClientSimulator:
    """模拟 PLC 客户端：连接软件的 9999/9009 并收发数据。"""

    def __init__(self, log: LogBus):
        self.log = log
        self.enqueue_sock: Optional[socket.socket] = None
        self.dequeue_sock: Optional[socket.socket] = None
        self.running = False
        self.feedback_thread: Optional[threading.Thread] = None

    def connect(self, host: str, enqueue_port: int, dequeue_port: int) -> bool:
        try:
            self.enqueue_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.enqueue_sock.connect((host, enqueue_port))

            self.dequeue_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.dequeue_sock.connect((host, dequeue_port))

            self.running = True
            self.feedback_thread = threading.Thread(target=self._feedback_loop, daemon=True)
            self.feedback_thread.start()

            self.log.push(f"PLC连接成功 -> {host}:{enqueue_port}/{dequeue_port}")
            return True
        except Exception as exc:
            self.log.push(f"PLC连接失败: {exc}")
            self.close()
            return False

    def _feedback_loop(self):
        """持续接收软件通过 9009 返回的反馈码（1N/1D/2N/2D）。"""
        sock = self.dequeue_sock
        if not sock:
            return

        while self.running:
            try:
                data = sock.recv(16)
                if not data:
                    self.log.push("PLC反馈连接断开")
                    break

                text = data.decode("ascii", errors="ignore")
                self.log.push(f"收到PLC反馈码: {text}")
            except Exception as exc:
                if self.running:
                    self.log.push(f"接收PLC反馈异常: {exc}")
                break

    def send_enqueue_command(self, command: int, count: int, pointer: int):
        if not self.enqueue_sock:
            self.log.push("PLC未连接入队通道")
            return

        payload = struct.pack(">HHH", command & 0xFFFF, count & 0xFFFF, pointer & 0xFFFF)
        self.enqueue_sock.sendall(payload)
        self.log.push(f"PLC发送入队: cmd={command:02X} count={count} pointer={pointer}")

    def send_dequeue_pointers(self, arm1_pointer: int, arm2_pointer: int):
        if not self.dequeue_sock:
            self.log.push("PLC未连接出队通道")
            return

        payload = struct.pack(">HH", arm1_pointer & 0xFFFF, arm2_pointer & 0xFFFF)
        self.dequeue_sock.sendall(payload)
        self.log.push(f"PLC发送出队指针: arm1={arm1_pointer} arm2={arm2_pointer}")

    def close(self):
        self.running = False
        for s in (self.enqueue_sock, self.dequeue_sock):
            if s:
                try:
                    s.close()
                except Exception:
                    pass
        self.enqueue_sock = None
        self.dequeue_sock = None


# =========================
# 相机服务端模拟器
# =========================
class CameraServerSimulator:
    """模拟相机服务端：等待软件连接，接收触发命令并返回解析结果。"""

    def __init__(self, host: str, port: int, log: LogBus, payload_builder: Callable[[int, Optional[int]], bytes]):
        self.host = host
        self.port = port
        self.log = log
        self.payload_builder = payload_builder

        self.server_sock: Optional[socket.socket] = None
        self.client_sock: Optional[socket.socket] = None
        self.running = False
        self.accept_thread: Optional[threading.Thread] = None

    def start(self):
        if self.running:
            return
        self.running = True
        self.accept_thread = threading.Thread(target=self._run_server, daemon=True)
        self.accept_thread.start()

    def _run_server(self):
        try:
            self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_sock.bind((self.host, self.port))
            self.server_sock.listen(1)
            self.log.push(f"相机模拟器监听: {self.host}:{self.port}")

            while self.running:
                client, addr = self.server_sock.accept()
                self.client_sock = client
                self.log.push(f"相机已连接软件: {addr}")
                self._handle_client(client)
        except Exception as exc:
            if self.running:
                self.log.push(f"相机服务异常: {exc}")

    def _parse_trigger(self, data: bytes):
        """兼容两种触发格式：
        1) 二进制（非并发）：command(2) 或 command(2)+count(2)
        2) ASCII（并发）：CMD:xx 或 CMD:xx,COUNT:n
        """
        # ASCII 模式
        try:
            text = data.decode("ascii")
            if text.startswith("CMD:"):
                m_cmd = re.search(r"CMD:([0-9A-Fa-f]{2})", text)
                m_cnt = re.search(r"COUNT:(\d+)", text)
                if m_cmd:
                    cmd = int(m_cmd.group(1), 16)
                    cnt = int(m_cnt.group(1)) if m_cnt else None
                    return cmd, cnt
        except Exception:
            pass

        # 二进制模式
        if len(data) >= 2:
            cmd = int.from_bytes(data[:2], byteorder="big")
            cnt = None
            if len(data) >= 4:
                cnt = int.from_bytes(data[2:4], byteorder="big")
            return cmd, cnt

        return None, None

    def _handle_client(self, client: socket.socket):
        while self.running:
            try:
                data = client.recv(1024)
                if not data:
                    self.log.push("相机连接断开")
                    break

                cmd, cnt = self._parse_trigger(data)
                self.log.push(f"相机收到触发: cmd={cmd} count={cnt}")

                # 仅对 01/11 返回数据；12通常不返回
                if cmd in (0x01, 0x11):
                    payload = self.payload_builder(cmd, cnt)
                    time.sleep(0.05)
                    client.sendall(payload)
                    self.log.push(f"相机回包: {payload!r}")
            except Exception as exc:
                if self.running:
                    self.log.push(f"相机处理异常: {exc}")
                break

    def stop(self):
        self.running = False
        for s in (self.client_sock, self.server_sock):
            if s:
                try:
                    s.close()
                except Exception:
                    pass
        self.client_sock = None
        self.server_sock = None


# =========================
# 机械臂服务端模拟器
# =========================
class ArmServerSimulator:
    """模拟机械臂服务端：接收软件下发数据，并按需发送 data/1 命令。"""

    def __init__(self, arm_id: int, host: str, port: int, log: LogBus):
        self.arm_id = arm_id
        self.host = host
        self.port = port
        self.log = log

        self.server_sock: Optional[socket.socket] = None
        self.client_sock: Optional[socket.socket] = None
        self.running = False
        self.accept_thread: Optional[threading.Thread] = None

        self.auto_complete = True

    def start(self):
        if self.running:
            return
        self.running = True
        self.accept_thread = threading.Thread(target=self._run_server, daemon=True)
        self.accept_thread.start()

    def _run_server(self):
        try:
            self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_sock.bind((self.host, self.port))
            self.server_sock.listen(1)
            self.log.push(f"机械臂{self.arm_id}模拟器监听: {self.host}:{self.port}")

            while self.running:
                client, addr = self.server_sock.accept()
                self.client_sock = client
                self.log.push(f"机械臂{self.arm_id}已连接软件: {addr}")
                self._recv_loop(client)
        except Exception as exc:
            if self.running:
                self.log.push(f"机械臂{self.arm_id}服务异常: {exc}")

    def _recv_loop(self, client: socket.socket):
        while self.running:
            try:
                data = client.recv(4096)
                if not data:
                    self.log.push(f"机械臂{self.arm_id}连接断开")
                    break

                self.log.push(f"机械臂{self.arm_id}收到数据({len(data)}字节)")

                if self.auto_complete:
                    time.sleep(0.05)
                    self.send_complete()
            except Exception as exc:
                if self.running:
                    self.log.push(f"机械臂{self.arm_id}接收异常: {exc}")
                break

    def send_request_data(self):
        if not self.client_sock:
            self.log.push(f"机械臂{self.arm_id}未连接，无法发送 data")
            return
        try:
            self.client_sock.sendall(b"data")
            self.log.push(f"机械臂{self.arm_id}发送命令: data")
        except Exception as exc:
            self.log.push(f"机械臂{self.arm_id}发送 data 失败: {exc}")

    def send_complete(self):
        if not self.client_sock:
            self.log.push(f"机械臂{self.arm_id}未连接，无法发送 1")
            return
        try:
            self.client_sock.sendall(b"1")
            self.log.push(f"机械臂{self.arm_id}发送命令: 1")
        except Exception as exc:
            self.log.push(f"机械臂{self.arm_id}发送 1 失败: {exc}")

    def is_connected_to_software(self) -> bool:
        """当前是否已有主程序连接到该机械臂模拟服务。"""
        return self.client_sock is not None

    def stop(self):
        self.running = False
        for s in (self.client_sock, self.server_sock):
            if s:
                try:
                    s.close()
                except Exception:
                    pass
        self.client_sock = None
        self.server_sock = None


# =========================
# GUI 主程序
# =========================
@dataclass
class SimState:
    sensor_count: int = 0
    enqueue_pointer: int = 1
    arm1_last_dequeue_pointer: int = 0
    arm2_last_dequeue_pointer: int = 0


class DeviceSimulatorApp:
    """联调模拟器主界面。"""

    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("设备联调模拟器（PLC/相机/机械臂）")
        self.root.geometry("1050x680")

        self.log_bus = LogBus()
        self.state = SimState()

        # 每次传感器触发后，两个机械臂都应有同一 pointer 的待出队项
        self.pending_arm1 = deque()
        self.pending_arm2 = deque()

        # GUI 控件变量
        self.software_host_var = tk.StringVar(value="127.0.0.1")
        self.enqueue_port_var = tk.IntVar(value=9999)
        self.dequeue_port_var = tk.IntVar(value=9009)

        self.camera_host_var = tk.StringVar(value="127.0.0.1")
        self.camera_port_var = tk.IntVar(value=8080)

        self.arm1_host_var = tk.StringVar(value="127.0.0.1")
        self.arm1_port_var = tk.IntVar(value=6001)
        self.arm2_host_var = tk.StringVar(value="127.0.0.1")
        self.arm2_port_var = tk.IntVar(value=6002)

        self.concurrent_mode_var = tk.BooleanVar(value=False)

        # 模拟器对象
        self.plc = PLCClientSimulator(self.log_bus)
        self.camera_sim: Optional[CameraServerSimulator] = None
        self.arm1_sim: Optional[ArmServerSimulator] = None
        self.arm2_sim: Optional[ArmServerSimulator] = None

        self._build_ui()
        self._start_device_servers()
        self._schedule_log_flush()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self):
        top = ttk.Frame(self.root, padding=10)
        top.pack(fill=tk.X)

        row1 = ttk.Frame(top)
        row1.pack(fill=tk.X, pady=(0, 6))

        ttk.Label(row1, text="软件地址:").pack(side=tk.LEFT)
        ttk.Entry(row1, textvariable=self.software_host_var, width=14).pack(side=tk.LEFT, padx=(4, 10))

        ttk.Label(row1, text="9999:").pack(side=tk.LEFT)
        ttk.Entry(row1, textvariable=self.enqueue_port_var, width=6).pack(side=tk.LEFT, padx=(4, 10))

        ttk.Label(row1, text="9009:").pack(side=tk.LEFT)
        ttk.Entry(row1, textvariable=self.dequeue_port_var, width=6).pack(side=tk.LEFT, padx=(4, 10))

        ttk.Checkbutton(
            row1,
            text="并发相机模式（ASCII触发）",
            variable=self.concurrent_mode_var,
        ).pack(side=tk.LEFT, padx=(8, 0))

        row2 = ttk.Frame(top)
        row2.pack(fill=tk.X, pady=(0, 8))

        ttk.Button(row2, text="重启相机/机械臂模拟服务", command=self._restart_device_servers).pack(side=tk.LEFT)
        ttk.Button(row2, text="连接PLC到软件", command=self._connect_plc).pack(side=tk.LEFT, padx=6)
        ttk.Button(row2, text="断开PLC", command=self._disconnect_plc).pack(side=tk.LEFT)

        cfg = ttk.LabelFrame(self.root, text="设备服务参数（需与主程序配置一致）", padding=10)
        cfg.pack(fill=tk.X, padx=10, pady=(0, 8))

        cfg_row = ttk.Frame(cfg)
        cfg_row.pack(fill=tk.X)

        ttk.Label(cfg_row, text="相机").pack(side=tk.LEFT)
        ttk.Entry(cfg_row, textvariable=self.camera_host_var, width=14).pack(side=tk.LEFT, padx=(4, 4))
        ttk.Entry(cfg_row, textvariable=self.camera_port_var, width=6).pack(side=tk.LEFT, padx=(0, 14))

        ttk.Label(cfg_row, text="机械臂1").pack(side=tk.LEFT)
        ttk.Entry(cfg_row, textvariable=self.arm1_host_var, width=14).pack(side=tk.LEFT, padx=(4, 4))
        ttk.Entry(cfg_row, textvariable=self.arm1_port_var, width=6).pack(side=tk.LEFT, padx=(0, 14))

        ttk.Label(cfg_row, text="机械臂2").pack(side=tk.LEFT)
        ttk.Entry(cfg_row, textvariable=self.arm2_host_var, width=14).pack(side=tk.LEFT, padx=(4, 4))
        ttk.Entry(cfg_row, textvariable=self.arm2_port_var, width=6).pack(side=tk.LEFT)

        plc_frame = ttk.LabelFrame(self.root, text="PLC操作按钮（核心）", padding=10)
        plc_frame.pack(fill=tk.X, padx=10, pady=(0, 8))

        ttk.Button(plc_frame, text="传感器触发", command=self._on_sensor_trigger).pack(side=tk.LEFT)
        ttk.Button(plc_frame, text="机械臂1出队", command=self._on_arm1_dequeue).pack(side=tk.LEFT, padx=6)
        ttk.Button(plc_frame, text="机械臂2出队", command=self._on_arm2_dequeue).pack(side=tk.LEFT)

        arm_frame = ttk.LabelFrame(self.root, text="机械臂辅助操作", padding=10)
        arm_frame.pack(fill=tk.X, padx=10, pady=(0, 8))

        ttk.Button(arm_frame, text="机械臂1手动data", command=lambda: self.arm1_sim and self.arm1_sim.send_request_data()).pack(side=tk.LEFT)
        ttk.Button(arm_frame, text="机械臂1手动1", command=lambda: self.arm1_sim and self.arm1_sim.send_complete()).pack(side=tk.LEFT, padx=6)
        ttk.Button(arm_frame, text="机械臂2手动data", command=lambda: self.arm2_sim and self.arm2_sim.send_request_data()).pack(side=tk.LEFT)
        ttk.Button(arm_frame, text="机械臂2手动1", command=lambda: self.arm2_sim and self.arm2_sim.send_complete()).pack(side=tk.LEFT, padx=6)

        self.auto_arm1_var = tk.BooleanVar(value=True)
        self.auto_arm2_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(arm_frame, text="机械臂1自动回1", variable=self.auto_arm1_var, command=self._sync_auto_complete).pack(side=tk.LEFT, padx=(20, 6))
        ttk.Checkbutton(arm_frame, text="机械臂2自动回1", variable=self.auto_arm2_var, command=self._sync_auto_complete).pack(side=tk.LEFT)

        self.status_var = tk.StringVar(value="状态：待连接")
        ttk.Label(self.root, textvariable=self.status_var, foreground="#0a5").pack(anchor="w", padx=12)

        log_frame = ttk.LabelFrame(self.root, text="运行日志", padding=8)
        log_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=8)

        self.log_text = tk.Text(log_frame, wrap="word", height=20)
        self.log_text.pack(fill=tk.BOTH, expand=True)

    def _sync_auto_complete(self):
        if self.arm1_sim:
            self.arm1_sim.auto_complete = self.auto_arm1_var.get()
        if self.arm2_sim:
            self.arm2_sim.auto_complete = self.auto_arm2_var.get()

    def _build_camera_payload(self, command: int, count: Optional[int]) -> bytes:
        """构造相机回包。

        这里使用 ASCII 便于观察：
        - ARM1: CAM|ARM:1|COUNT:n|PTS:...
        - ARM2: CAM|ARM:2|COUNT:n|PTS:...
        """
        arm_id = 1 if command == 0x01 else 2
        if count is None:
            count = 0
        payload = f"CAM|ARM:{arm_id}|COUNT:{count}|PTS:1.1,2.2,3.3|E"
        return payload.encode("ascii")

    def _start_device_servers(self):
        """启动相机与机械臂模拟服务端。"""
        try:
            self.camera_sim = CameraServerSimulator(
                host=self.camera_host_var.get().strip(),
                port=int(self.camera_port_var.get()),
                log=self.log_bus,
                payload_builder=self._build_camera_payload,
            )
            self.camera_sim.start()

            self.arm1_sim = ArmServerSimulator(
                arm_id=1,
                host=self.arm1_host_var.get().strip(),
                port=int(self.arm1_port_var.get()),
                log=self.log_bus,
            )
            self.arm1_sim.start()

            self.arm2_sim = ArmServerSimulator(
                arm_id=2,
                host=self.arm2_host_var.get().strip(),
                port=int(self.arm2_port_var.get()),
                log=self.log_bus,
            )
            self.arm2_sim.start()

            self._sync_auto_complete()
            self.log_bus.push("相机/机械臂模拟服务已启动")
        except Exception as exc:
            self.log_bus.push(f"启动模拟服务失败: {exc}")

    def _stop_device_servers(self):
        for sim in (self.camera_sim, self.arm1_sim, self.arm2_sim):
            if sim:
                sim.stop()
        self.camera_sim = None
        self.arm1_sim = None
        self.arm2_sim = None

    def _restart_device_servers(self):
        self._stop_device_servers()
        time.sleep(0.05)
        self._start_device_servers()

    def _connect_plc(self):
        ok = self.plc.connect(
            host=self.software_host_var.get().strip(),
            enqueue_port=int(self.enqueue_port_var.get()),
            dequeue_port=int(self.dequeue_port_var.get()),
        )
        if ok:
            self.status_var.set("状态：PLC已连接")
        else:
            self.status_var.set("状态：PLC连接失败")

    def _disconnect_plc(self):
        self.plc.close()
        self.status_var.set("状态：PLC已断开")

    def _on_sensor_trigger(self):
        """模拟传感器触发：
        1) PLC发送01（创建新工件）
        2) PLC发送11（第二机械臂流程）
        3) PLC发送12（流程结束信号）
        4) 维护本地计数与待出队指针队列
        """
        count = self.state.sensor_count
        pointer = self.state.enqueue_pointer

        try:
            self.plc.send_enqueue_command(0x01, count, pointer)
            time.sleep(0.01)
            self.plc.send_enqueue_command(0x11, count, pointer)
            time.sleep(0.01)
            self.plc.send_enqueue_command(0x12, count, pointer)

            self.pending_arm1.append(pointer)
            self.pending_arm2.append(pointer)

            self.state.sensor_count += 1
            self.state.enqueue_pointer += 1

            self.log_bus.push(
                f"传感器触发完成: count={count} pointer={pointer}（已加入arm1/arm2待出队）"
            )
        except Exception as exc:
            self.log_bus.push(f"传感器触发失败: {exc}")

    def _on_arm1_dequeue(self):
        """机械臂1出队按钮：
        1) 更新 arm1 出队指针并发送到 PLC 9009
        2) 机械臂1立即发送 data 请求
        """
        if not self.pending_arm1:
            self.log_bus.push("机械臂1无待出队指针")
            return

        if not self.arm1_sim or not self.arm1_sim.is_connected_to_software():
            self.log_bus.push("机械臂1未与主程序建立连接，已阻止出队（指针未消耗）")
            return

        pointer = self.pending_arm1.popleft()
        self.state.arm1_last_dequeue_pointer = pointer

        self.plc.send_dequeue_pointers(
            self.state.arm1_last_dequeue_pointer,
            self.state.arm2_last_dequeue_pointer,
        )

        if self.arm1_sim:
            self.arm1_sim.send_request_data()

    def _on_arm2_dequeue(self):
        """机械臂2出队按钮：
        1) 更新 arm2 出队指针并发送到 PLC 9009
        2) 机械臂2立即发送 data 请求
        """
        if not self.pending_arm2:
            self.log_bus.push("机械臂2无待出队指针")
            return

        if not self.arm2_sim or not self.arm2_sim.is_connected_to_software():
            self.log_bus.push("机械臂2未与主程序建立连接，已阻止出队（指针未消耗）")
            return

        pointer = self.pending_arm2.popleft()
        self.state.arm2_last_dequeue_pointer = pointer

        self.plc.send_dequeue_pointers(
            self.state.arm1_last_dequeue_pointer,
            self.state.arm2_last_dequeue_pointer,
        )

        if self.arm2_sim:
            self.arm2_sim.send_request_data()

    def _schedule_log_flush(self):
        for line in self.log_bus.drain():
            self.log_text.insert(tk.END, line + "\n")
            self.log_text.see(tk.END)
        self.root.after(100, self._schedule_log_flush)

    def _on_close(self):
        self.plc.close()
        self._stop_device_servers()
        self.root.destroy()


def main():
    root = tk.Tk()
    app = DeviceSimulatorApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
