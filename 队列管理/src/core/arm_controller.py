"""
机械臂通信模块（增强版）。

职责：
1. 维护多个机械臂 TCP 连接（支持主动连接 / 被动监听两种模式）。
2. 接收机械臂命令（data / 1）并回调给上层。
3. 向机械臂发送点位数据（支持 bytes 或 str）。

原始通讯记录：
    在详细日志模式（detailed）下，会记录通讯的原始字节内容。
    调用 get_dequeue_logger() 获取专用日志器。
"""

from __future__ import annotations

import logging
import re
import socket
import threading
import time
from dataclasses import dataclass
from enum import Enum
from typing import Callable, Dict, List, Optional, Union

from src.utils.logger import get_dequeue_logger, log_raw_data

logger = logging.getLogger(__name__)


class ArmStatus(Enum):
    """机械臂连接状态。"""

    DISCONNECTED = "DISCONNECTED"
    LISTENING = "LISTENING"
    CONNECTING = "CONNECTING"
    CONNECTED = "CONNECTED"
    BUSY = "BUSY"
    ERROR = "ERROR"


@dataclass
class ArmInfo:
    """单机械臂连接信息。"""

    id: int
    host: str
    port: int
    name: str
    bind_host: str = "0.0.0.0"
    status: ArmStatus = ArmStatus.DISCONNECTED
    sock: Optional[socket.socket] = None
    server_sock: Optional[socket.socket] = None
    client_addr: Optional[tuple] = None
    last_heartbeat: float = 0.0


class SimpleArmController:
    """机械臂网络控制器。"""

    def __init__(
        self,
        arms_config: Union[List[Dict], Dict[int, Dict]],
        connection_mode: str = "active",
        default_listen_host: str = "0.0.0.0",
    ):
        self.arms: Dict[int, ArmInfo] = {}
        self.running = False
        self.command_callback: Optional[Callable[[int, str], None]] = None
        self.receiver_threads: Dict[int, threading.Thread] = {}
        self.accept_threads: Dict[int, threading.Thread] = {}
        self.arm_locks: Dict[int, threading.Lock] = {}
        self.reconnect_thread: Optional[threading.Thread] = None
        self.reconnect_interval = 2.0
        self.default_listen_host = default_listen_host

        mode = (connection_mode or "active").strip().lower()
        if mode not in ("active", "passive"):
            raise ValueError(f"unsupported arm connection_mode: {connection_mode}")
        self.connection_mode = mode

        # 支持两种配置格式：
        # 1) 列表：[{id,host,port,name}, ...]
        # 2) 字典：{id: {host,port,name}, ...}
        normalized = self._normalize_config(arms_config)
        for cfg in normalized:
            arm = ArmInfo(
                id=cfg["id"],
                host=cfg["host"],
                port=cfg["port"],
                name=cfg.get("name", f"Arm_{cfg['id']}"),
                bind_host=cfg.get("bind_host", self.default_listen_host),
            )
            self.arms[arm.id] = arm
            self.arm_locks[arm.id] = threading.Lock()

    def _normalize_config(self, arms_config: Union[List[Dict], Dict[int, Dict]]) -> List[Dict]:
        """将不同配置格式归一化为列表。"""
        if isinstance(arms_config, dict):
            out = []
            for arm_id, cfg in arms_config.items():
                item = dict(cfg)
                item.setdefault("id", arm_id)
                out.append(item)
            return out
        return list(arms_config)

    def set_command_callback(self, callback: Callable[[int, str], None]):
        """设置命令回调（上层主控注入）。"""
        self.command_callback = callback

    def connect_all(self) -> dict:
        """初始化所有机械臂链路（主动连接或被动监听）。"""
        self.running = True
        success_count = 0
        connected_count = 0
        errors = []

        if self.connection_mode == "active":
            for arm in self.arms.values():
                if self._connect_arm(arm):
                    success_count += 1
                    connected_count += 1
                    self._start_receiver(arm)
                else:
                    errors.append(f"{arm.name} 连接失败")

            # 自动重连线程常驻运行：
            # 1) 启动时连接失败时自动补连
            # 2) 运行中断线后自动恢复
            if self.reconnect_thread is None or not self.reconnect_thread.is_alive():
                self.reconnect_thread = threading.Thread(
                    target=self._reconnect_loop,
                    daemon=True,
                    name="arm-reconnect-loop",
                )
                self.reconnect_thread.start()
        else:
            # 被动模式：软件监听端口，等待机械臂主动连接
            for arm in self.arms.values():
                if self._start_arm_listener(arm):
                    success_count += 1
                else:
                    errors.append(f"{arm.name} 监听失败")

        return {
            "success": success_count == len(self.arms),
            "connected_count": connected_count,
            "listening_count": success_count if self.connection_mode == "passive" else 0,
            "total_count": len(self.arms),
            "errors": errors,
            "mode": self.connection_mode,
        }

    def _connect_arm(self, arm: ArmInfo) -> bool:
        """连接单个机械臂。"""
        if self.connection_mode != "active":
            return self._start_arm_listener(arm)

        lock = self.arm_locks.setdefault(arm.id, threading.Lock())
        with lock:
            if arm.status == ArmStatus.CONNECTED and arm.sock is not None:
                return True

            if arm.sock is not None:
                try:
                    arm.sock.close()
                except Exception:
                    pass
                arm.sock = None

        try:
            arm.status = ArmStatus.CONNECTING
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(3.0)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            sock.connect((arm.host, arm.port))
            # 连接阶段需要超时，进入业务收发阶段改回阻塞模式，避免空闲被误判为断线。
            sock.settimeout(None)

            arm.sock = sock
            arm.status = ArmStatus.CONNECTED
            arm.last_heartbeat = time.time()
            logger.info("[机械臂] 已连接 机械臂=%s 地址=%s:%s", arm.id, arm.host, arm.port)
            return True
        except Exception:
            arm.status = ArmStatus.ERROR
            logger.warning("[机械臂] 连接失败 机械臂=%s 地址=%s:%s", arm.id, arm.host, arm.port)
            return False

    def _start_arm_listener(self, arm: ArmInfo) -> bool:
        """被动模式：在本机端口启动监听，等待机械臂连接。"""
        lock = self.arm_locks.setdefault(arm.id, threading.Lock())
        with lock:
            if arm.server_sock is not None:
                return True

            try:
                server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                server.bind((arm.bind_host, arm.port))
                server.listen(5)
                server.settimeout(1.0)
                arm.server_sock = server
                arm.status = ArmStatus.LISTENING
                logger.info(
                    "[机械臂] 监听中 机械臂=%s 监听=%s:%s 期望对端=%s",
                    arm.id,
                    arm.bind_host,
                    arm.port,
                    arm.host,
                )
            except Exception:
                arm.status = ArmStatus.ERROR
                logger.warning(
                    "[机械臂] 监听失败 机械臂=%s 监听=%s:%s",
                    arm.id,
                    arm.bind_host,
                    arm.port,
                )
                return False

        old_thread = self.accept_threads.get(arm.id)
        if old_thread and old_thread.is_alive():
            return True

        thread = threading.Thread(
            target=self._accept_loop,
            args=(arm,),
            daemon=True,
            name=f"arm-accept-{arm.id}",
        )
        self.accept_threads[arm.id] = thread
        thread.start()
        return True

    def _accept_loop(self, arm: ArmInfo):
        """被动模式接收机械臂连接。"""
        while self.running and arm.server_sock:
            try:
                client, addr = arm.server_sock.accept()
                self._attach_passive_connection(arm, client, addr)
            except socket.timeout:
                continue
            except OSError:
                break
            except Exception:
                logger.warning("[机械臂] 接收连接失败 机械臂=%s", arm.id)
                time.sleep(0.2)

    def _attach_passive_connection(self, arm: ArmInfo, client: socket.socket, addr: tuple):
        """将新接入客户端绑定为当前机械臂连接。"""
        lock = self.arm_locks.setdefault(arm.id, threading.Lock())
        with lock:
            # 避免“重复建连”导致旧连接被立刻踢掉：
            # 现场某些机械臂程序会在多线程中重复connect，
            # 若每次都替换旧连接，会造成对端recv读到空（连接被关闭）。
            # 这里优先保留“仍可用”的旧连接；若旧连接已假活，则允许新连接接管。
            if arm.sock and arm.status == ArmStatus.CONNECTED:
                if self._is_socket_alive(arm.sock):
                    try:
                        client.close()
                    except Exception:
                        pass
                    logger.warning(
                        "[机械臂] 丢弃重复连接 机械臂=%s 新连接=%s 保留连接=%s",
                        arm.id,
                        addr,
                        arm.client_addr,
                    )
                    return

                logger.warning(
                    "[机械臂] 替换失效连接 机械臂=%s 旧连接=%s 新连接=%s",
                    arm.id,
                    arm.client_addr,
                    addr,
                )
                try:
                    arm.sock.close()
                except Exception:
                    pass
                arm.sock = None
            # 原连接不可用时，允许新连接接管。
            if arm.sock:
                try:
                    arm.sock.close()
                except Exception:
                    pass
                arm.sock = None

            client.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            client.settimeout(None)
            arm.sock = client
            arm.client_addr = addr
            arm.status = ArmStatus.CONNECTED
            arm.last_heartbeat = time.time()

        peer_ip = addr[0] if addr else ""
        if arm.host and arm.host != "0.0.0.0" and peer_ip and arm.host != peer_ip:
            logger.warning(
                "[机械臂] 连接来源与配置不符 机械臂=%s 实际来源=%s 期望来源=%s",
                arm.id,
                peer_ip,
                arm.host,
            )
        else:
            logger.info("[机械臂] 已接入 机械臂=%s 来源=%s", arm.id, addr)

        self._start_receiver(arm)

    def _is_socket_alive(self, sock: Optional[socket.socket]) -> bool:
        """轻量探测 socket 活性：可读到EOF视为断开，阻塞/超时视为仍可用。"""
        if sock is None:
            return False

        previous_timeout = None
        try:
            previous_timeout = sock.gettimeout()
        except Exception:
            previous_timeout = None

        try:
            sock.settimeout(0.0)
            try:
                data = sock.recv(1, socket.MSG_PEEK)
                return data != b""
            except (BlockingIOError, InterruptedError, socket.timeout):
                return True
            except OSError:
                return False
        finally:
            try:
                sock.settimeout(previous_timeout)
            except Exception:
                pass

    def _start_receiver(self, arm: ArmInfo):
        """为机械臂启动接收线程。"""
        old_thread = self.receiver_threads.get(arm.id)
        if old_thread and old_thread.is_alive():
            return

        thread = threading.Thread(
            target=self._receive_loop,
            args=(arm,),
            daemon=True,
            name=f"arm-recv-{arm.id}",
        )
        self.receiver_threads[arm.id] = thread
        thread.start()

    def _receive_loop(self, arm: ArmInfo):
        """循环接收机械臂指令并转发给上层。"""
        # 获取出队专用日志器（详细模式下会写入 dequeue.log）
        dequeue_logger = get_dequeue_logger()
        
        while self.running:
            if arm.status != ArmStatus.CONNECTED or arm.sock is None:
                break

            try:
                data = arm.sock.recv(1024)
                if not data:
                    self._handle_disconnect(arm)
                    break

                # 记录接收的原始内容（详细日志模式下）
                client_addr = f"{arm.client_addr[0]}:{arm.client_addr[1]}" if arm.client_addr else "unknown"
                raw_hex = data.hex().upper()
                log_raw_data(dequeue_logger, "RECV", client_addr, raw_hex, f"机械臂={arm.id}")
                
                text = data.decode("utf-8", errors="ignore")
                commands = self._extract_commands(text)
                if self.command_callback:
                    for command in commands:
                        self.command_callback(arm.id, command)
            except socket.timeout:
                # 若外部修改了超时配置，保持线程存活并继续接收。
                continue
            except Exception:
                self._handle_disconnect(arm)
                break

    def _extract_commands(self, raw_text: str) -> List[str]:
        """
        从一次 recv 文本中提取机械臂命令列表。

        支持命令：
        - data   : 请求缓存数据
        - ok     : 机械臂收到数据后的完成确认（花都现场协议）
        - 1      : 完成确认（兼容旧协议）
        - 1,count / 1 count / 1:count : 带计数的完成确认

        说明：
        - 现场TCP可能粘包，一次recv内可能含多条命令；
        - 也可能没有明显分隔符（如"datadata"），此时用正则连续提取。
        - 去重规则：同一批次内多条 data 只保留第一条，防止向 PLC 多次发送 XN
          导致 IO 信号额外脉冲（高→低→高→低），让机械臂误以为收到新的 IO 触发
          而跳过后续工件。
        """
        text = (raw_text or "").strip()
        if not text:
            return []

        pattern = re.compile(r"data|1(?:\s*[,:\s]\s*\d+)?", flags=re.IGNORECASE)
        all_cmds = [m.group(0).strip() for m in pattern.finditer(text)]

        # 去重规则：
        #   两条 data 之间有 1（完成确认）→ 第二条 data 属于新一轮，保留
        #   两条 data 之间没有 1           → 第二条 data 是粘包重复，丢弃
        # 判断依据是命令序列中的 1 是否出现，与时间无关。
        result: List[str] = []
        pending_data = False   # 已收到 data 且尚未收到对应的 1
        dup_data_count = 0
        for cmd in all_cmds:
            if cmd.lower() == "data":
                if not pending_data:
                    result.append(cmd)
                    pending_data = True
                else:
                    dup_data_count += 1
            else:
                # 收到完成确认（1 / 1,count），本轮结束，下次 data 视为新一轮
                result.append(cmd)
                pending_data = False

        if dup_data_count:
            logger.warning(
                "[机械臂] 同一TCP批次检测到 %d 条重复 data 命令，已丢弃（原始内容: %r）",
                dup_data_count,
                raw_text,
            )

        return result

    def send_data(self, arm_id: int, data: Union[str, bytes]) -> bool:
        """向指定机械臂发送数据。"""
        # 获取出队专用日志器（详细模式下会写入 dequeue.log）
        dequeue_logger = get_dequeue_logger()
        
        arm = self.arms.get(arm_id)
        if not arm or arm.status != ArmStatus.CONNECTED or arm.sock is None:
            logger.warning(
                "[机械臂] 跳过发送 机械臂=%s 状态=%s 已连接=%s",
                arm_id,
                arm.status.value if arm else "MISSING",
                bool(arm and arm.sock is not None),
            )
            return False

        try:
            arm.status = ArmStatus.BUSY
            # 二进制 payload（机械臂点位包）直接发送；字符串用 latin-1 编码。
            payload = data if isinstance(data, bytes) else data.encode("latin-1")
            
            # 记录发送的原始内容（详细日志模式下）
            client_addr = f"{arm.client_addr[0]}:{arm.client_addr[1]}" if arm.client_addr else "unknown"
            raw_hex = payload.hex().upper()
            log_raw_data(dequeue_logger, "SEND", client_addr, raw_hex, f"机械臂={arm_id} 字节数={len(payload)}")
            
            arm.sock.sendall(payload)
            arm.status = ArmStatus.CONNECTED
            logger.debug(
                "[机械臂] 发送成功 机械臂=%s 字节数=%s 对端=%s",
                arm_id,
                len(payload),
                arm.client_addr,
            )
            return True
        except Exception as exc:
            arm.status = ArmStatus.ERROR
            logger.warning(
                "[机械臂] 发送失败 机械臂=%s 对端=%s 错误=%s",
                arm_id,
                arm.client_addr,
                exc,
            )
            return False

    def _handle_disconnect(self, arm: ArmInfo):
        """处理机械臂断链。"""
        if arm.sock:
            try:
                arm.sock.close()
            except Exception:
                pass
            arm.sock = None
        arm.client_addr = None
        if self.connection_mode == "passive" and arm.server_sock is not None:
            arm.status = ArmStatus.LISTENING
        else:
            arm.status = ArmStatus.DISCONNECTED
        logger.warning("[机械臂] 已断开 机械臂=%s", arm.id)

    def _reconnect_loop(self):
        """后台重连循环。"""
        while self.running:
            for arm in self.arms.values():
                if arm.status in (ArmStatus.DISCONNECTED, ArmStatus.ERROR):
                    if self._connect_arm(arm):
                        self._start_receiver(arm)
            time.sleep(self.reconnect_interval)

    def get_all_status(self) -> Dict[int, str]:
        """返回所有机械臂状态文本。"""
        return {arm_id: arm.status.value for arm_id, arm in self.arms.items()}

    def stop(self):
        """停止接收并关闭所有连接。"""
        self.running = False

        if self.connection_mode == "active" and self.reconnect_thread and self.reconnect_thread.is_alive():
            self.reconnect_thread.join(timeout=1.0)

        for arm in self.arms.values():
            if arm.sock:
                try:
                    arm.sock.close()
                except Exception:
                    pass
                arm.sock = None
            if arm.server_sock:
                try:
                    arm.server_sock.close()
                except Exception:
                    pass
                arm.server_sock = None
            arm.status = ArmStatus.DISCONNECTED
            arm.client_addr = None

        for thread in self.receiver_threads.values():
            if thread.is_alive():
                thread.join(timeout=0.5)
        self.receiver_threads.clear()

        for thread in self.accept_threads.values():
            if thread.is_alive():
                thread.join(timeout=0.5)
        self.accept_threads.clear()
