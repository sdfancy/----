"""
新入队流程协调器（11/12 + 2D/3D）。

流程摘要：
    1. PLC(9999) 发 11+count+pointer -> 软件创建队列项并给 3D 发 "11,count"。
    2. 2D 先回 READY（窗口：11~12）-> 软件发 "(count)E" 请求数据 -> 2D 回 "count,type1,type2"。
    3. PLC(9999) 发 12+count+pointer -> 立即给 3D 发 "12,count,type1,type2"；
       若该 count 尚无 2D 类型，则回退 "12,count,00,00"（不等待）。
    4. 3D 回 "(... )EA(... )E" -> 按 flag 千位分流到 arm1/arm2 队列，完成后回 PLC "Done"。

数据流说明：
    入队触发：
        PLC 发送 11 命令 -> 创建 EnqueueCycle -> 向 3D 相机发送触发命令
        
    2D 相机流程：
        2D 返回 READY -> 软件发送数据请求 -> 2D 返回类型数据
        
    3D 相机流程：
        3D 返回点位数据 -> 解析并写入队列 -> 发送完成握手给 PLC
        
    完成握手：
        收到所有相机数据后，发送 Done 给 PLC
"""

import logging
import re
import threading
from dataclasses import dataclass, field
from typing import Dict, Optional, Set

from src.core.camera_server import DualCameraServer
from src.core.plc_handler import DualPortPLCHandler
from src.core.queue_manager import EnhancedQueueManager

logger = logging.getLogger(__name__)


@dataclass
class EnqueueCycle:
    """
    入队周期数据（对应 PLC 的一次 11->12 流程）。
    
    属性说明：
        - count: PLC 发送的计数值，用于唯一标识这个入队周期
        - pointer: PLC 发送的脉冲指针
        - part_type: 工件类型（从 2D 相机获取）
        - ready_received: 是否已收到 2D 相机的 READY 信号
        - request_sent_to_2d: 是否已向 2D 相机发送数据请求
        - end_received: 是否已收到 PLC 的 12 结束命令
        - end_sent_to_3d: 是否已向 3D 相机发送结束命令
        - done_sent_to_plc: 是否已向 PLC 发送完成握手
        - arm_ready: 已准备好数据的机械臂编号集合（1=arm1, 2=arm2）
    """
    count: int
    pointer: int
    part_type: Optional[str] = None
    ready_received: bool = False
    request_sent_to_2d: bool = False
    end_received: bool = False
    end_sent_to_3d: bool = False
    done_sent_to_plc: bool = False
    arm_ready: Set[int] = field(default_factory=set)


class EnqueueWorkflowCoordinator:
    """11/12 + 2D/3D 的入队流程状态机。"""

    def __init__(
        self,
        queue_manager: EnhancedQueueManager,
        plc_handler: DualPortPLCHandler,
        camera_server: DualCameraServer,
        unknown_enqueue_policy: str = "ignore",
    ):
        self.queue_manager = queue_manager
        self.plc_handler = plc_handler
        self.camera_server = camera_server
        self.unknown_enqueue_policy = str(unknown_enqueue_policy or "ignore").strip().lower()

        self._lock = threading.Lock()
        self._cycles: Dict[int, EnqueueCycle] = {}
        self._recv_buffers: Dict[str, bytes] = {"2d": b"", "3d": b""}
        # 2D 是串行设备：仅允许一个有效 READY 窗口（11 到 12）
        self._active_2d_window_count: Optional[int] = None

    def get_status(self) -> Dict[str, object]:
        with self._lock:
            active_cycles = []
            for cycle in self._cycles.values():
                active_cycles.append(
                    {
                        "count": cycle.count,
                        "pointer": cycle.pointer,
                        "part_type": cycle.part_type,
                        "ready_received": cycle.ready_received,
                        "request_sent_to_2d": cycle.request_sent_to_2d,
                        "end_received": cycle.end_received,
                        "end_sent_to_3d": cycle.end_sent_to_3d,
                        "arm_ready": sorted(list(cycle.arm_ready)),
                    }
                )
            active_cycles.sort(key=lambda x: x["count"])
            return {
                "unknown_enqueue_policy": self.unknown_enqueue_policy,
                "active_2d_window_count": self._active_2d_window_count,
                "active_cycle_count": len(active_cycles),
                "active_cycles": active_cycles,
            }

    def handle_plc_enqueue(self, command: int, sensor_count: int, pulse_pointer: int):
        """
        处理 PLC 的入队命令（11 或 12）。
        
        命令说明：
            - 11：入队开始命令，触发 3D 相机拍照
            - 12：入队结束命令，通知相机流程结束
            
        协议兼容：
            支持十进制（11/12）或十六进制（0x11/0x12）两种写法。
            
        未知命令处理：
            - 默认（ignore）：忽略未知命令
            - legacy：回退到旧的入队逻辑
            
        Args:
            command: PLC 命令码（11 或 12）
            sensor_count: 传感器计数值
            pulse_pointer: 脉冲指针
        """
        # 兼容两种写法：十进制11/12 或 0x11/0x12
        if command in (11, 0x11):
            self._handle_start(sensor_count, pulse_pointer)
            return
        if command in (12, 0x12):
            self._handle_end(sensor_count, pulse_pointer)
            return

        if self.unknown_enqueue_policy == "legacy":
            # 兼容模式：未知命令回退旧逻辑。
            self.queue_manager.handle_plc_command(command, sensor_count, pulse_pointer)
            return
        logger.info("[流程] 忽略入队命令 命令=%s 计数=%s 指针=%s", command, sensor_count, pulse_pointer)

    def on_camera_payload(self, camera_key: str, payload: bytes):
        if not payload:
            return
        camera_key = str(camera_key).strip().lower()
        if camera_key not in ("2d", "3d"):
            return

        with self._lock:
            self._recv_buffers[camera_key] += payload

        if camera_key == "2d":
            self._drain_2d_frames()
        else:
            self._drain_3d_frames()

    def _handle_start(self, sensor_count: int, pulse_pointer: int):
        """
        处理入队开始命令（PLC 发送 11）。
        
        处理流程：
            1. 创建一个新的 EnqueueCycle，记录 count 和 pointer
            2. 记录当前活跃的 2D 窗口（用于判断 READY 是否有效）
            3. 调用队列管理器的入队接口创建队列项
            4. 向 3D 相机发送触发命令 "11,count"
            
        Args:
            sensor_count: PLC 发送的计数值
            pulse_pointer: PLC 发送的脉冲指针
        """
        with self._lock:
            cycle = EnqueueCycle(count=int(sensor_count), pointer=int(pulse_pointer))
            self._cycles[int(sensor_count)] = cycle
            self._active_2d_window_count = int(sensor_count)

        self.queue_manager.enqueue_item(sensor_count=int(sensor_count), pulse_pointer=int(pulse_pointer))
        self.camera_server.send_to_camera("3d", f"11,{int(sensor_count)}".encode("ascii"))
        logger.info("[流程] 入队开始 计数=%s 指针=%s", sensor_count, pulse_pointer)

    def _handle_end(self, sensor_count: int, pulse_pointer: int):
        """
        处理入队结束命令（PLC 发送 12）。
        
        处理流程：
            1. 查找或创建对应的 EnqueueCycle
            2. 标记 end_received=True
            3. 关闭 2D READY 窗口（后续 READY 无效）
            4. 如果没有收到 2D 类型数据，立即发送 fallback（00,00）给 3D
            5. 否则正常发送结束命令给 3D
            
        重要设计：
            - 12 命令到达后不等待 2D 数据，立即继续流程
            - 如果 2D 数据还没到，使用 fallback 触发 3D
            - 晚到的 2D 数据会被丢弃
            
        Args:
            sensor_count: PLC 发送的计数值
            pulse_pointer: PLC 发送的脉冲指针
        """
        send_fallback = False
        with self._lock:
            cycle = self._cycles.get(int(sensor_count))
            if cycle is None:
                cycle = EnqueueCycle(count=int(sensor_count), pointer=int(pulse_pointer))
                self._cycles[int(sensor_count)] = cycle
            cycle.pointer = int(pulse_pointer)
            cycle.end_received = True
            # 2D READY 窗口：12 到来即关闭，后续 READY 直接丢弃，等下次11再开窗。
            if self._active_2d_window_count == int(sensor_count):
                self._active_2d_window_count = None
            send_fallback = not bool(cycle.part_type)

        # 节拍优先：12 到来后不阻塞等待 2D。
        # 若此刻无 2D 类型，立即以 00,00 触发 3D 结束，后到的 2D 结果按晚到丢弃处理。
        if send_fallback:
            logger.info("[流程] 收到结束命令 计数=%s 未收到2D结果，使用默认类型00,00", sensor_count)
            self._send_3d_end(sensor_count=int(sensor_count), part_type_override="00,00")
            return
        self._send_3d_end(sensor_count=int(sensor_count))

    def _send_3d_end(self, sensor_count: int, part_type_override: Optional[str] = None):
        payload = b""
        type_text = ""
        with self._lock:
            cycle = self._cycles.get(int(sensor_count))
            if not cycle or cycle.end_sent_to_3d:
                return
            cycle.end_sent_to_3d = True
            type_text = self._normalize_2d_type(part_type_override or cycle.part_type)
            payload = f"12,{cycle.count},{type_text}".encode("ascii")

        sent = self.camera_server.send_to_camera("3d", payload)
        if not sent:
            with self._lock:
                cycle = self._cycles.get(int(sensor_count))
                if cycle is not None:
                    cycle.end_sent_to_3d = False
            logger.warning("[流程] 发送3D结束命令失败 计数=%s 数据=%s", sensor_count, payload)
            return
        logger.info("[流程] 已发送3D结束命令 数据=%s", payload.decode("ascii", errors="ignore"))

    def _drain_2d_frames(self):
        while True:
            with self._lock:
                frame = self._pop_2d_frame_locked()
                if frame is None:
                    return
            self._handle_2d_frame(frame)

    def _handle_2d_frame(self, frame: bytes):
        parsed = self._parse_2d_payload(frame)
        if parsed is None:
            logger.warning("[流程] 2D数据帧无效：%r", frame)
            return

        kind = parsed[0]
        if kind == "READY":
            self._handle_2d_ready()
            return

        _, sensor_count, part_type = parsed
        self.camera_server.send_to_camera("2d", b"OK")

        with self._lock:
            cycle = self._cycles.get(sensor_count)
            if cycle is None:
                logger.warning("[流程] 丢弃过期2D结果 计数=%s 数据=%r", sensor_count, frame)
                return
            if cycle.end_sent_to_3d:
                logger.info("[流程] 丢弃晚到2D结果 计数=%s", sensor_count)
                return
            cycle.part_type = part_type
            should_send = cycle.end_received and (not cycle.end_sent_to_3d)

        logger.info("[流程] 收到2D结果 计数=%s 类型=%s", sensor_count, part_type)
        if should_send:
            self._send_3d_end(sensor_count=sensor_count)

    def _parse_2d_payload(self, payload: bytes) -> Optional[tuple]:
        text = payload.decode("utf-8", errors="ignore").strip()
        if not text:
            return None
        upper_text = text.upper()
        if upper_text in ("READY", "(READY)E", "READYE", "(READY)"):
            return ("READY",)

        if text.endswith("E"):
            text = text[:-1].strip()
        if text.startswith("(") and text.endswith(")"):
            text = text[1:-1].strip()

        if text.upper() == "READY":
            return ("READY",)

        parts = [p.strip() for p in text.split(",")]
        if not parts:
            return None
        try:
            sensor_count = int(parts[0])
        except ValueError:
            match = re.search(r"\d+", parts[0])
            if not match:
                return None
            sensor_count = int(match.group(0))

        type_fields = [p for p in parts[1:] if p != ""]
        part_type = ",".join(type_fields) if type_fields else "00,00"
        return "RESULT", sensor_count, part_type

    def _handle_2d_ready(self):
        request_payload = b""
        target_count: Optional[int] = None
        with self._lock:
            target_count = self._active_2d_window_count
            if target_count is None:
                logger.info("[流程] 丢弃过期2D READY：没有活动窗口")
                return
            cycle = self._cycles.get(int(target_count))
            if cycle is None:
                logger.info("[流程] 丢弃过期2D READY：流程不存在 计数=%s", target_count)
                return
            if cycle.end_received:
                logger.info("[流程] 丢弃过期2D READY：窗口已关闭 计数=%s", target_count)
                return
            if cycle.request_sent_to_2d:
                logger.info("[流程] 忽略重复2D READY 计数=%s", target_count)
                return
            cycle.ready_received = True
            cycle.request_sent_to_2d = True
            request_payload = f"({int(target_count)})E".encode("ascii")

        sent = self.camera_server.send_to_camera("2d", request_payload)
        if not sent:
            with self._lock:
                cycle = self._cycles.get(int(target_count))
                if cycle is not None:
                    cycle.request_sent_to_2d = False
            logger.warning("[流程] 请求2D数据失败 计数=%s", target_count)
            return
        logger.info("[流程] 接受2D READY 计数=%s 已发送请求=%s", target_count, request_payload)

    def _pop_2d_frame_locked(self) -> Optional[bytes]:
        buf = self._recv_buffers["2d"]
        if not buf:
            return None

        buf = buf.lstrip(b"\r\n\t ")
        self._recv_buffers["2d"] = buf
        if not buf:
            return None

        # 优先按历史协议 "(...)E" 截帧，确保旧格式不会被 CSV 兜底分支误切分。
        end_idx = buf.find(b")E")
        if end_idx >= 0:
            frame = buf[: end_idx + 2]
            self._recv_buffers["2d"] = buf[end_idx + 2 :]
            return frame

        # 兼容纯CSV行（例如 "123,00,00\n"）和 READY 行。
        newline_idx = -1
        newline_len = 0
        for sep in (b"\r\n", b"\n", b"\r"):
            idx = buf.find(sep)
            if idx >= 0 and (newline_idx < 0 or idx < newline_idx):
                newline_idx = idx
                newline_len = len(sep)
        if newline_idx >= 0:
            frame = buf[:newline_idx]
            self._recv_buffers["2d"] = buf[newline_idx + newline_len :]
            frame = frame.strip()
            if frame:
                return frame
            return None

        # 无换行/无 )E 的情况下，只在“看起来像完整2D文本帧”时整体交给解析器，
        # 避免把半包提前消费掉。
        candidate = buf.strip()
        if self._looks_like_2d_line(candidate):
            self._recv_buffers["2d"] = b""
            return candidate
        return None

    def _looks_like_2d_line(self, payload: bytes) -> bool:
        if not payload:
            return False
        text = payload.decode("utf-8", errors="ignore").strip()
        if not text:
            return False
        if len(text) > 128:
            return False
        if text.upper() in ("READY", "(READY)", "(READY)E", "READYE"):
            return True
        parts = [p.strip() for p in text.split(",")]
        if not parts:
            return False
        if not re.fullmatch(r"\d+", parts[0]):
            return False
        return True

    def _normalize_2d_type(self, value: Optional[str]) -> str:
        text = str(value or "").strip()
        return text if text else "00,00"

    def _drain_3d_frames(self):
        # 3D 帧格式是 "(...)EA(...)E"，一次可能收多帧或半帧。
        while True:
            with self._lock:
                raw = self._recv_buffers["3d"]
                text = raw.decode("latin-1", errors="ignore")
                # 仅在缓冲区中找到“一整帧双臂数据”时才出队，半包继续留在缓冲区等待补齐。
                match = re.search(r"\([^()]*\)EA\([^()]*\)E", text)
                if not match:
                    return
                frame_text = text[match.start() : match.end()]
                frame = frame_text.encode("latin-1", errors="ignore")
                self._recv_buffers["3d"] = text[match.end() :].encode("latin-1", errors="ignore")
            self._handle_3d_frame(frame)

    def _handle_3d_frame(self, frame: bytes):
        text = frame.decode("utf-8", errors="ignore").strip()
        segments = [s for s in text.split("A") if s.strip()]
        touched_counts: Set[int] = set()

        for segment in segments:
            parsed = self._parse_3d_segment(segment)
            if parsed is None:
                logger.warning("[流程] 3D数据段无效：%s", segment)
                continue
            arm_id, sensor_count, payload_bytes = parsed
            with self._lock:
                cycle = self._cycles.get(sensor_count)
                if cycle is None:
                    logger.warning("[流程] 未找到3D计数：%s", sensor_count)
                    continue
                pointer = cycle.pointer
            ok = self.queue_manager.store_camera_data(
                sensor_count=sensor_count,
                pulse_pointer=pointer,
                camera_data=payload_bytes,
                arm_id=arm_id,
            )
            if not ok:
                continue
            with self._lock:
                cycle = self._cycles.get(sensor_count)
                if cycle is None:
                    continue
                cycle.arm_ready.add(arm_id)
                touched_counts.add(sensor_count)

        # 同一 count 只有 arm1/arm2 都就绪才允许回 Done，避免只入单臂就提前收尾。
        for sensor_count in touched_counts:
            self._try_send_done(sensor_count)

    def _parse_3d_segment(self, segment: str) -> Optional[tuple]:
        text = segment.strip()
        if not text:
            return None
        if not text.endswith("E"):
            text = f"{text}E"
        body = text[:-1].strip()
        if body.startswith("(") and body.endswith(")"):
            body = body[1:-1].strip()

        tokens = [t.strip() for t in body.split(",") if t.strip()]
        if len(tokens) < 2:
            return None

        try:
            flag = int(tokens[0])
            sensor_count = int(tokens[1])
        except ValueError:
            return None

        arm_id = flag // 1000
        if arm_id not in (1, 2):
            return None

        payload_bytes = text.encode("latin-1", errors="ignore")
        return arm_id, sensor_count, payload_bytes

    def _try_send_done(self, sensor_count: int):
        with self._lock:
            cycle = self._cycles.get(sensor_count)
            if cycle is None:
                return
            # 幂等保护：Done 只发一次。
            if cycle.done_sent_to_plc:
                return
            if not ({1, 2}.issubset(cycle.arm_ready)):
                return
            cycle.done_sent_to_plc = True
            count = cycle.count
            pointer = cycle.pointer
            self._cycles.pop(sensor_count, None)

        self.plc_handler.send_enqueue_feedback("Done")
        logger.info("[流程] 入队完成 计数=%s 指针=%s", count, pointer)
