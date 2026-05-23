"""
队列管理模块（增强版，双队列版本）。

核心设计：
1. 维护 arm1 / arm2 两个数据队列，二者由同一工件触发同步入队。
2. 入队以 PLC 的 01 命令为准；11/12 不创建新工件，仅用于相机流程控制。
3. 支持两种超时策略：cycle_timeout 与 dequeue_timeout。
4. 机械臂协议：data -> 发送缓存并反馈 XN；1 -> 完成并反馈 XD。

数据流向概述：
    入队流程（PLC 01命令）：
        PLC 发送 01 命令 -> 创建新工件（双臂同步）-> 等待相机数据 -> 相机数据写入队列
    
    出队流程（机械臂请求）：
        机械臂发送 data 请求 -> 检查出队缓存 -> 发送数据并反馈 XN
        机械臂发送 1 完成确认 -> 更新队列状态 -> 反馈 XD
    
    预取机制：
        当 PLC 出队指针变化时，根据 prefetch_offset 预取下一个数据到缓存，
        减少机械臂请求时的等待时间。

超时处理：
    - cycle_timeout: 新工件开始前，将上一轮未完成的相机槽位填默认值
    - dequeue_timeout: 队列项在队列中停留超时后标记为超时
"""

import logging
import struct
import threading
import time
from collections import deque
from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Optional

logger = logging.getLogger(__name__)


@dataclass
class ArmQueueItem:
    """单机械臂队列项。"""

    pointer: int
    count: int
    enqueue_seq: int
    created_at: float
    updated_at: float
    data: Optional[bytes] = None
    status: str = "WAITING_CAMERA"
    timed_out: bool = False
    dequeued: bool = False
    send_count: int = 0
    confirmed: bool = False
    source: str = "pending"   # pending/camera/default


@dataclass
class DequeueCache:
    """机械臂出队缓存。"""

    queue_item: ArmQueueItem
    arm_id: int
    trigger_pointer: int = 0
    prefetch_offset: int = 0
    data_sent: bool = False
    confirmed: bool = False
    cache_time: float = 0.0
    send_count: int = 0


class EnhancedQueueManager:
    """增强版双队列管理器。"""

    def __init__(
        self,
        length: int = 10,
        timeout_mode: str = "cycle_timeout",
        camera_correlation_mode: str = "sequential",
        default_payloads: Optional[Dict[int, Any]] = None,
        drop_late_camera_data: bool = True,
        history_limit: int = 1000,
        history_display_limit: int = 100,
        prefetch_offset: int = 0,
        prefetch_drop_first: bool = True,
        completion_count_validation: bool = True,
        send_default_on_empty_request: bool = False,
    ):
        self.length = length
        self.lock = threading.Lock()

        # 两个数据队列：key 为入队 pointer
        self.arm_data_queues: Dict[int, Dict[int, ArmQueueItem]] = {
            1: {},
            2: {},
        }

        # 为兼容旧调用，保留 queue 指向 arm1 队列视图
        self.queue = self.arm_data_queues[1]

        self.arm_positions: Dict[int, int] = {}
        self.arm_caches: Dict[int, DequeueCache] = {}
        self.arm_statuses: Dict[int, str] = {}

        self.camera_callback: Optional[Callable[[int, int], None]] = None
        self.plc_feedback_callback: Optional[Callable[[str], None]] = None
        # 入队阶段握手反馈（例如相机1到达后回"11"，相机2到达后回"21"）
        self.plc_stage_callback: Optional[Callable[[str], None]] = None

        self.last_dequeue_pointers: Dict[int, int] = {}

        # 最近一次有效 01 的上下文
        self.current_cycle_count: Optional[int] = None
        # 最近一次触发相机时使用的命令（01 或 11）
        # 用于区分相机返回的数据属于哪个机械臂
        # 发送 01 后设置为 1，发送 11 后设置为 2
        self.last_trigger_command: Optional[int] = None

        # 简化模式：当前等待时的 count 值（用于超时填充默认值）
        self.current_waiting_count: Optional[int] = None
        # 简化模式：当前等待时的 pointer 值
        self.current_waiting_pointer: Optional[int] = None

        self._enqueue_seq_counter = 0

        self.timeout_mode = timeout_mode
        self.camera_correlation_mode = camera_correlation_mode
        self.drop_late_camera_data = drop_late_camera_data
        self.history_limit = max(10, int(history_limit))
        self.history_display_limit = max(10, int(history_display_limit))
        self.prefetch_offset = max(0, int(prefetch_offset))
        self.prefetch_drop_first = bool(prefetch_drop_first)
        self.completion_count_validation = bool(completion_count_validation)
        self.send_default_on_empty_request = bool(send_default_on_empty_request)
        self.event_history = deque(maxlen=self.history_limit)
        self._history_seq = 0
        self.arm_prefetch_warmed: Dict[int, bool] = {1: False, 2: False}
        self.arm_last_confirmed_count: Dict[int, Optional[int]] = {1: None, 2: None}

        default_payloads = default_payloads or {1: "(1000)E", 2: "(2000)E"}
        self.default_payloads: Dict[int, bytes] = {
            arm_id: self._normalize_default_payload(value)
            for arm_id, value in default_payloads.items()
        }

        self.stats = {
            "total_items": 0,
            "validation_warnings": 0,
            "completed_tasks": 0,
            "dequeue_operations": 0,
            "overflow_drops": 0,
            "timeout_fills": 0,
            "late_camera_discards": 0,
        }

        logger.info(
            "[队列] 双队列管理器已初始化 长度=%s 超时模式=%s 相机关联模式=%s 预取偏移=%s",
            length,
            timeout_mode,
            camera_correlation_mode,
            self.prefetch_offset,
        )

    def _normalize_default_payload(self, value: Any) -> bytes:
        if isinstance(value, bytes):
            return value
        if isinstance(value, str):
            return value.encode("latin-1")
        return str(value).encode("latin-1")

    def _default_marker_for_arm(self, arm_id: int) -> int:
        raw = self.default_payloads.get(arm_id, b"(0000)E")
        try:
            text = raw.decode("latin-1", errors="ignore")
        except Exception:
            return 0
        digits = "".join(ch for ch in text if ch.isdigit())
        if not digits:
            return 0
        # 约定默认码是 4 位（1000/2000），过长时取前 4 位。
        return int(digits[:4])

    def _default_payload_for_arm(self, arm_id: int, count: Optional[int] = None) -> bytes:
        """
        生成机械臂的默认填充数据。

        参数:
            arm_id: 机械臂编号 (1 或 2)
            count: 工件的计数值。如果为 None，则使用 0（表示默认值/无数据）

        返回:
            格式化后的默认数据，如 "(1000,5)E" 或 "(1000,0)E"

        说明:
            - count 有值时：表示该工件的实际计数值，用于超时填充场景
            - count 为 0 时（count=None）：表示这是默认值，用于出队时无缓存场景
            - 机械臂可通过 count 是否为 0 来判断数据是否为默认值
        """
        marker = self._default_marker_for_arm(arm_id)
        if count is None:
            return f"({marker:04d},0)E".encode("latin-1")
        return f"({marker:04d},{int(count)})E".encode("latin-1")

    def _fill_both_arms_default_locked(self):
        """填充arm1和arm2队列的默认值（相机数据超时场景）"""
        pointer = self.current_waiting_pointer
        count = self.current_waiting_count
        
        for arm_id in (1, 2):
            queue_item = self.arm_data_queues.get(arm_id, {}).get(pointer)
            if queue_item and queue_item.data is None:
                queue_item.data = self._default_payload_for_arm(arm_id, count)
                queue_item.source = "default"
                queue_item.status = "TIMEOUT_DEFAULT"
                logger.warning("[队列] 超时填充默认值 机械臂=%s 计数=%s 指针=%s", arm_id, count, pointer)
        
        self.current_waiting_pointer = None
        self.current_waiting_count = None

    def _fill_default_for_slot_locked(self, arm_id: int, pointer: int, reason: str):
        """填充单个队列项的默认值（超时场景）"""
        queue_item = self.arm_data_queues.get(arm_id, {}).get(pointer)
        if not queue_item:
            return

        if queue_item.data is not None:
            return

        queue_item.data = self._default_payload_for_arm(arm_id, queue_item.count)
        queue_item.source = "default"
        queue_item.status = f"TIMEOUT_DEFAULT({reason})"
        logger.warning("[队列] 超时填充默认值 机械臂=%s 指针=%s 原因=%s", arm_id, pointer, reason)

    def _format_history_data(self, payload: Any, max_bytes: int = 256) -> str:
        """将原始数据转为可读字符串（文本/结构化优先，最后才显示字节数组）。"""
        if payload is None:
            return ""

        if isinstance(payload, (bytes, bytearray)):
            raw = bytes(payload)
            if not raw:
                return ""

            # 1) 优先识别可读文本（例如 "(1000)E" 或 ASCII 协议帧）
            printable = sum(1 for b in raw if (32 <= b <= 126) or b in (9, 10, 13))
            if (printable / len(raw)) >= 0.85:
                text = raw.decode("utf-8", errors="ignore").strip()
                if not text:
                    text = raw.decode("latin-1", errors="ignore").strip()
                text = text.replace("\r", " ").replace("\n", "\\n")
                if len(text) > max_bytes:
                    text = f"{text[:max_bytes]}...(+{len(raw) - max_bytes}B)"
                return f"文本: {text}"

            # 2) 识别机械臂下发格式：flag(2)+count(2)+pointer(2)+points(float...)+E
            if raw[-1:] == b"E" and len(raw) >= 7 and ((len(raw) - 7) % 4 == 0):
                try:
                    flag, sensor_count, pulse_pointer = struct.unpack("!HHH", raw[:6])
                    points: List[float] = []
                    point_data = raw[6:-1]
                    for i in range(0, len(point_data), 4):
                        points.append(round(struct.unpack("!f", point_data[i : i + 4])[0], 4))
                    points_preview = points[:6]
                    points_text = ", ".join(str(p) for p in points_preview)
                    if len(points) > len(points_preview):
                        points_text += f", ...(+{len(points) - len(points_preview)}点)"
                    return (
                        f"机械臂数据: flag={flag}, count={sensor_count}, "
                        f"pointer={pulse_pointer}, points=[{points_text}]"
                    )
                except Exception:
                    pass

            # 3) 识别 PLC 常见帧
            if len(raw) == 4:
                arm1_pointer = int.from_bytes(raw[:2], byteorder="big")
                arm2_pointer = int.from_bytes(raw[2:], byteorder="big")
                return f"PLC出队指针: arm1={arm1_pointer}, arm2={arm2_pointer}"

            if len(raw) == 2:
                command = int.from_bytes(raw, byteorder="big")
                return f"PLC命令: command={command} (0x{command:04X})"

            if len(raw) >= 6 and raw[-1:] != b"E":
                command, field2, field3 = struct.unpack("!HHH", raw[:6])
                extra = len(raw) - 6
                suffix = f", extra={extra}B" if extra > 0 else ""
                return f"PLC入队帧: command={command}, field2={field2}, field3={field3}{suffix}"

            # 4) 最后兜底：十进制字节数组（比16进制更直观）
            clip_len = min(len(raw), 48)
            byte_list = ", ".join(str(v) for v in raw[:clip_len])
            if len(raw) > clip_len:
                byte_list += f", ...(+{len(raw) - clip_len}B)"
            return f"字节数组(len={len(raw)}): [{byte_list}]"

        return str(payload)

    def _preview_queue_data(self, payload: Optional[bytes], max_bytes: int = 16) -> str:
        """队列表格中的原始数据预览：仅展示前 max_bytes 个字节。"""
        if not payload:
            return "-"

        raw = bytes(payload)

        # 1) 可打印文本优先（例如 "(1000)E"）
        printable = sum(1 for b in raw if (32 <= b <= 126) or b in (9, 10, 13))
        if raw and (printable / len(raw)) >= 0.85:
            text = raw.decode("utf-8", errors="ignore").strip()
            if not text:
                text = raw.decode("latin-1", errors="ignore").strip()
            text = text.replace("\r", " ").replace("\n", " ")
            if len(text) > max_bytes:
                return f"{text[:max_bytes]}..."
            return text or "-"

        # 2) 机械臂结构化帧：flag(2)+count(2)+pointer(2)+...+E
        if raw[-1:] == b"E" and len(raw) >= 7 and ((len(raw) - 7) % 4 == 0):
            try:
                flag, sensor_count, pulse_pointer = struct.unpack("!HHH", raw[:6])
                return f"flag={flag},count={sensor_count},ptr={pulse_pointer}"
            except Exception:
                pass

        # 3) 兜底：十进制字节数组
        preview = ", ".join(str(v) for v in raw[:max_bytes])
        if len(raw) > max_bytes:
            return f"[{preview}, ...]"
        return f"[{preview}]"

    def _normalize_history_data_for_hmi(self, value: Any) -> str:
        """兼容历史旧格式：清理曾经写入的 ASCII码 前缀/后缀。"""
        if value is None:
            return ""
        text = str(value)

        if text.startswith("ASCII文本: "):
            text = text.replace("ASCII文本: ", "文本: ", 1)

        marker = "; ASCII码:"
        if marker in text:
            text = text.split(marker, 1)[0]

        if text.startswith("ASCII码(len="):
            text = text.replace("ASCII码", "字节数组", 1)

        return text

    def _append_history_locked(
        self,
        event: str,
        message: str,
        arm_id: Optional[int] = None,
        pointer: Optional[int] = None,
        count: Optional[int] = None,
        level: str = "INFO",
        history_data: Optional[str] = None,
    ):
        """记录历史事件（由调用方保证已经持有 self.lock）。"""
        self._history_seq += 1
        self.event_history.append(
            {
                "seq": self._history_seq,
                "ts": time.time(),
                "event": event,
                "level": level,
                "arm_id": arm_id,
                "pointer": pointer,
                "count": count,
                "message": message,
                "history_data": history_data or "",
            }
        )

    def set_callbacks(self, camera_cb=None, plc_feedback_cb=None, plc_stage_cb=None):
        """设置跨模块回调。"""
        self.camera_callback = camera_cb
        self.plc_feedback_callback = plc_feedback_cb
        self.plc_stage_callback = plc_stage_cb
        logger.info("[队列] 回调已配置")

    def enqueue_item(self, sensor_count: int, pulse_pointer: int) -> bool:
        """
        低阶入队接口：直接创建两个队列项（用于测试与兼容）。
        
        Args:
            sensor_count: 传感器计数值
            pulse_pointer: 脉冲指针
            
        Note:
            此接口直接入队，不经过 PLC 命令解析，通常用于测试或旧协议兼容。
        """
        with self.lock:
            self._enqueue_pair_locked(sensor_count, pulse_pointer)
        return True

    def handle_plc_command(self, command: int, sensor_count: int, pulse_pointer: int) -> bool:
        """
        简化版 PLC 命令处理。

        流程：
        - 01：入队 → 等待 arm1 数据 → 收到发 1Done
        - 11：等待 arm2 数据 → 收到发 2Done
        - 12：结束
        """
        normalized_command = self._normalize_plc_command(command)

        with self.lock:
            if normalized_command == 0x01:
                if self.current_waiting_pointer is not None:
                    self._fill_both_arms_default_locked()
                
                self._enqueue_pair_locked(sensor_count, pulse_pointer)
                self.current_waiting_pointer = pulse_pointer
                self.current_waiting_count = sensor_count
                self.current_cycle_count = sensor_count
                trigger_count = sensor_count

            elif normalized_command == 0x0B:
                self.current_waiting_pointer = pulse_pointer
                self.current_waiting_count = self.current_cycle_count or sensor_count
                trigger_count = self.current_waiting_count

            elif normalized_command == 0x0C:
                self.current_waiting_pointer = None
                trigger_count = self.current_cycle_count or sensor_count

            else:
                trigger_count = sensor_count

        # 触发相机
        if self.camera_callback:
            try:
                self.camera_callback(normalized_command, trigger_count)
            except Exception as exc:
                logger.warning("[队列] 相机回调失败：%s", exc)

        return True

    def _normalize_plc_command(self, command: int) -> int:
        """
        归一PLC命令值。

        处理规则：
        - 1 -> 0x01：相机需要二进制格式
        - 11 / 12：原样透传（PLC 发送 0x0B/0x0C，解析为 11/12，直接传给相机）
        - 其余命令保持原值透传
        """
        if command == 1:
            return 0x01
        return command

    def _enqueue_pair_locked(self, sensor_count: int, pulse_pointer: int):
        warning = self._validate_pointer_consistency(sensor_count, pulse_pointer)

        # 双队列同步入队：以 arm1 队列容量作为统一容量判断
        is_new_pointer = pulse_pointer not in self.arm_data_queues[1]
        if is_new_pointer and len(self.arm_data_queues[1]) >= self.length:
            self._remove_oldest_pair_locked()

        self._enqueue_seq_counter += 1
        now = time.time()

        for arm_id in (1, 2):
            self.arm_data_queues[arm_id][pulse_pointer] = ArmQueueItem(
                pointer=pulse_pointer,
                count=sensor_count,
                enqueue_seq=self._enqueue_seq_counter,
                created_at=now,
                updated_at=now,
                status="WAITING_CAMERA",
                source="pending",
            )

        self.stats["total_items"] += 1

        logger.info(
            "[队列] 双队列入队 计数=%s 指针=%s 有告警=%s",
            sensor_count,
            pulse_pointer,
            bool(warning),
        )
        self._append_history_locked(
            event="enqueue",
            message="新工件入队",
            pointer=pulse_pointer,
            count=sensor_count,
        )

    def store_camera_data(self, sensor_count: int, pulse_pointer: int, camera_data: bytes, arm_id: int = 1) -> bool:
        """
        按 arm_id + pointer 直接写入相机数据（兼容测试与人工注入）。
        
        处理流程：
            1. 根据 arm_id 和 pointer 查找对应的队列项
            2. 检查数据是否已过期（已出队或已超时）：
               - 如果已出队且配置了丢弃 late 数据，则丢弃
               - 如果已超时且配置了丢弃 late 数据，则丢弃
            3. 将相机数据写入队列项
            4. 更新队列项状态为 CAMERA_READY
            
        注意：
            - 如果队列项不存在（可能被清理），返回 False
            - 写入成功后，队列项就可以被机械臂请求了
            
        Args:
            sensor_count: 相机/传感器计数值
            pulse_pointer: 脉冲指针（用于匹配队列项）
            camera_data: 相机原始数据
            arm_id: 机械臂编号（1 或 2），默认 1
            
        Returns:
            bool: True 表示写入成功，False 表示失败（队列项不存在或数据已过期）
        """
        with self.lock:
            queue_item = self.arm_data_queues.get(arm_id, {}).get(pulse_pointer)
            if not queue_item:
                logger.warning("[队列] 保存相机数据失败 未找到队列项 机械臂=%s 指针=%s", arm_id, pulse_pointer)
                return False

            if queue_item.dequeued and self.drop_late_camera_data:
                self.stats["late_camera_discards"] += 1
                return False

            if queue_item.timed_out and self.drop_late_camera_data:
                self.stats["late_camera_discards"] += 1
                return False

            queue_item.data = camera_data
            queue_item.updated_at = time.time()
            queue_item.status = "CAMERA_READY"
            queue_item.source = "camera"
            return True

    def _maybe_wrap_camera_data(self, camera_data: bytes, camera_count: Optional[int]) -> bytes:
        """
        对花都 2D 相机的文本回包进行格式化包装：插入 count 后形成 (flag,count,data)E。

        简化逻辑：查找第一个英文逗号，在后面插入 count 即可。
        格式：(标志位,count,点位数据)E

        例如：
            - 原始数据："(1001,点位数据)E"
            - 插入 count 后："(1001,5,点位数据)E"

        判断是否需要插入的逻辑：
            - 如果 camera_count 参数不为 None，说明相机已带 count，直接返回原始数据
            - 如果 camera_count 参数为 None（需要从 slot 获取），才执行插入

        参数:
            camera_data: 相机返回的原始字节数据
            camera_count: 要插入的 count 值。
                          - 如果不为 None：相机已带 count，不插入
                          - 如果为 None：需要插入 count

        返回:
            格式化后的相机数据字节
        """
        # camera_count 为 None 表示不需要插入，直接返回原始数据
        if not camera_data or camera_count is None:
            return camera_data

        try:
            text = camera_data.decode("utf-8", errors="ignore").strip()
            if not text:
                return camera_data

            # 去掉末尾的结束标记 'E'
            if text.endswith("E"):
                text = text[:-1]

            # 查找第一个逗号的位置
            comma_idx = text.find(",")
            if comma_idx == -1:
                return camera_data

            # 检查第二个字段是否已经是数字（即已包含 count）
            # 格式示例：(1001,5,点位数据)E -> parts[1] = "5"
            # 如果 parts[1] 能转成数字，说明已经带 count，不需要再插入
            parts = text.split(",")
            if len(parts) >= 2:
                try:
                    int(parts[1])  # 能转数字说明已带 count
                    return camera_data  # 直接返回，不插入
                except ValueError:
                    # 不能转数字，说明第二个字段是点位数据，需要插入 count
                    pass

            # 执行插入：在第一个逗号后面插入 count
            # 例如：(1001,点位数据)E -> (1001,5,点位数据)E
            new_text = text[:comma_idx + 1] + str(camera_count) + "," + text[comma_idx + 1:]
            return (new_text + "E").encode("utf-8")

        except Exception:
            return camera_data

    def _extract_arm_id_from_camera_data(self, camera_data: bytes) -> Optional[int]:
        """
        从相机返回的数据中解析标志位千位，确定机械臂ID。
        
        格式示例：(1234,...)E 或 (2234,...)E
        第一个数字（千位）是1或2，表示机械臂1或机械臂2
        
        返回：1 或 2，如果解析失败返回 None
        """
        if not camera_data:
            return None
        
        try:
            text = camera_data.decode("utf-8", errors="ignore").strip()
            if not text:
                return None
            
            # 去掉末尾的 E（如果有）
            if text.endswith("E"):
                text = text[:-1]
            
            # 查找第一个括号
            if text.startswith("("):
                # 取括号内第一个字符
                content = text[1:]
                if content and content[0].isdigit():
                    flag_digit = int(content[0])
                    if flag_digit in (1, 2):
                        return flag_digit
            
            return None
        except Exception:
            return None

    def ingest_camera_data(self, camera_data: bytes, camera_count: Optional[int] = None) -> bool:
        """
        简化版相机数据接收。

        流程：
        - 从相机数据中解析 arm_id（标志位）
        - 存入对应队列
        - 发送 1Done 或 2Done
        - 清除等待状态
        """
        with self.lock:
            arm_id = self._extract_arm_id_from_camera_data(camera_data)
            if arm_id is None:
                logger.warning("[队列] 丢弃相机数据：无法识别机械臂编号")
                return False

            pointer = self.current_waiting_pointer

            # 存入对应队列
            queue_item = self.arm_data_queues.get(arm_id, {}).get(pointer)
            if not queue_item:
                logger.warning("[队列] 丢弃相机数据：未找到队列项 机械臂=%s 指针=%s", arm_id, pointer)
                return False

            queue_item.data = camera_data
            queue_item.source = "camera"
            queue_item.status = "CAMERA_DATA"

            logger.info("[队列] 收到相机数据 机械臂=%s 指针=%s", arm_id, pointer)

            # 发送 Done
            if arm_id == 1:
                self._send_plc_stage_feedback("1Done")
            elif arm_id == 2:
                self._send_plc_stage_feedback("2Done")

            # 清除等待状态
            self.current_waiting_pointer = None

            return True

    def handle_dequeue_pointers(self, pointer_data: bytes):
        """
        处理出队指针数据（4字节：arm1指针2字节 + arm2指针2字节）。
        
        处理流程：
            1. 解析 4 字节数据，提取 arm1 和 arm2 的出队指针
            2. 与上次记录的指针比较，检测哪些指针发生了变化
            3. 仅对发生变化的指针调用 _process_arm_dequeue_locked 创建新缓存
            
        重要设计：
            - 只有指针变化才推进出队缓存，机械臂重复发送 data 请求不会触发新出队
            - 同一帧中的双臂缓存在一次锁内完成，避免 arm1/arm2 之间被 data 请求插入
            - 使用大端序（big endian）解析指针字节
        
        Args:
            pointer_data: 4字节的指针数据，前2字节为arm1指针，后2字节为arm2指针
        """
        if len(pointer_data) != 4:
            logger.warning("[队列] 出队指针报文长度无效：%s", len(pointer_data))
            return

        arm_pointers = {
            1: int.from_bytes(pointer_data[:2], byteorder="big"),
            2: int.from_bytes(pointer_data[2:], byteorder="big"),
        }

        changed: List[tuple] = []
        with self.lock:
            for arm_id, pointer in arm_pointers.items():
                if pointer != self.last_dequeue_pointers.get(arm_id, 0):
                    self.last_dequeue_pointers[arm_id] = pointer
                    changed.append((arm_id, pointer))
            # 仅“指针变化”才推进出队缓存；机械臂重复 data 请求不会在这里触发新出队。
            # 关键：同一帧中的双臂缓存在一次锁内完成，避免 arm1/arm2 之间被 data 请求插入。
            for arm_id, pointer in changed:
                self._process_arm_dequeue_locked(arm_id, pointer)

    def _process_arm_dequeue(self, arm_id: int, target_pointer: int):
        """
        为机械臂创建出队缓存（外部调用入口，带锁）。
        
        Args:
            arm_id: 机械臂编号（1 或 2）
            target_pointer: 目标出队指针
        """
        with self.lock:
            self._process_arm_dequeue_locked(arm_id, target_pointer)

    def _process_arm_dequeue_locked(self, arm_id: int, target_pointer: int):
        """
        为机械臂创建出队缓存（调用方需已持有 self.lock）。
        
        处理流程：
            1. 根据指针查找队列中对应的数据项
            2. 如果找到，调用预取逻辑选择实际发送的数据
            3. 创建/更新 DequeueCache 缓存
            4. 如果未找到，记录告警并更新状态为 NO_DATA_AVAILABLE
            
        预取机制：
            - prefetch_offset=0: 直接使用目标指针的数据
            - prefetch_offset=1: 使用目标指针+1 的数据（预取1拍）
            - 以此类推
            
        Args:
            arm_id: 机械臂编号（1 或 2）
            target_pointer: 目标出队指针
        """
        target_item = self.arm_data_queues.get(arm_id, {}).get(target_pointer)
        if not target_item:
            self._update_arm_status_locked(arm_id, "NO_DATA_AVAILABLE")
            logger.warning("[队列] 未找到出队指针 机械臂=%s 指针=%s", arm_id, target_pointer)
            self._append_history_locked(
                event="dequeue_miss",
                message="出队指针对应队列项不存在",
                arm_id=arm_id,
                pointer=target_pointer,
                level="WARN",
            )
            return

        selected_item, applied_offset = self._select_prefetch_item_locked(arm_id, target_item)
        if selected_item is None:
            # 预取模式下，目标前方数据尚未就绪：不创建缓存，等待下一拍。
            self.arm_caches.pop(arm_id, None)
            self.arm_positions[arm_id] = target_pointer
            self._update_arm_status_locked(arm_id, "NO_DATA_AVAILABLE")
            self._append_history_locked(
                event="prefetch_wait",
                message="预取目标尚未入队，当前拍不下发数据",
                arm_id=arm_id,
                pointer=target_pointer,
                count=target_item.count,
                level="WARN",
            )
            return

        # 缓存以“最新 PLC 出队事件”为准，同一机械臂旧缓存会被覆盖。
        self.arm_caches[arm_id] = DequeueCache(
            queue_item=selected_item,
            arm_id=arm_id,
            trigger_pointer=target_pointer,
            prefetch_offset=applied_offset,
            cache_time=time.time(),
        )
        self.arm_positions[arm_id] = target_pointer

        # dequeue_timeout 语义：
        # 当该元素“应该出队”（即 PLC 指针到达并触发出队）时，
        # 若相机数据仍未到，则立即填默认值。
        if self.timeout_mode == "dequeue_timeout" and selected_item.data is None:
            self._fill_default_for_slot_locked(arm_id, selected_item.pointer, "dequeue_pointer_reached")

        self._update_arm_status_locked(arm_id, "WAITING_FOR_REQUEST")
        self.stats["dequeue_operations"] += 1

        logger.info(
            "[队列] 出队缓存已准备 机械臂=%s 触发指针=%s 发送指针=%s 偏移=%s",
            arm_id,
            target_pointer,
            selected_item.pointer,
            applied_offset,
        )
        self._append_history_locked(
            event="dequeue_cache",
            message=f"出队缓存已准备（预取偏移={applied_offset}）",
            arm_id=arm_id,
            pointer=target_pointer,
            count=selected_item.count,
            history_data=self._format_history_data(selected_item.data),
        )

    def _select_prefetch_item_locked(self, arm_id: int, target_item: ArmQueueItem) -> tuple:
        """
        选择本次应下发的数据项。

        - prefetch_offset=0：下发当前目标项（原逻辑）
        - prefetch_offset=1：下发下一项（预取一拍）
        """
        if self.prefetch_offset <= 0:
            return target_item, 0

        ordered_items = sorted(
            self.arm_data_queues.get(arm_id, {}).values(),
            key=lambda x: x.enqueue_seq,
        )
        if not ordered_items:
            return None, self.prefetch_offset

        base_idx = None
        for idx, item in enumerate(ordered_items):
            if item.enqueue_seq == target_item.enqueue_seq and item.pointer == target_item.pointer:
                base_idx = idx
                break
        if base_idx is None:
            return None, self.prefetch_offset

        candidate_idx = base_idx + self.prefetch_offset
        if candidate_idx < len(ordered_items):
            self.arm_prefetch_warmed[arm_id] = True
            return ordered_items[candidate_idx], self.prefetch_offset

        # 预取暖机：首件允许空转一拍，避免“总是执行上一件数据”。
        if self.prefetch_drop_first and not self.arm_prefetch_warmed.get(arm_id, False):
            self.arm_prefetch_warmed[arm_id] = True
            return None, self.prefetch_offset

        return None, self.prefetch_offset

    def handle_arm_data_request(self, arm_id: int) -> Optional[bytes]:
        """
        处理机械臂的 data 请求（请求点位数据）。
        
        处理流程：
            1. 检查出队缓存（arm_caches）中是否存在有效缓存
            2. 如果没有缓存：
               - 根据配置决定是否返回默认值
               - 发送 Done 握手码让 PLC 继续流程
            3. 如果有缓存：
               - 检查数据是否已到达（相机数据）
               - 如果没有数据，根据超时模式处理
               - 提取缓存中的数据并发送
            4. 发送完成后，检查 PLC 出队指针是否已变化，若变化则刷新缓存
            
        重要设计：
            - data 请求不改变"出队选择"，同一缓存周期内重复 data 会得到同一条数据
            - 机械臂可能因为 IO 信号触发多次 data 请求，只有 PLC 指针变化才会更新缓存
            
        握手协议：
            - 发送 XN 反馈码（如 1N 表示 arm1 正常数据）
            - 无缓存时发送 Done 握手码让 PLC 继续
            
        Args:
            arm_id: 机械臂编号（1 或 2）
            
        Returns:
            bytes: 要发送的点位数据，如果没有则返回 None
        """
        payload = None
        history_pointer: Optional[int] = None
        history_count: Optional[int] = None
        history_message = ""
        with self.lock:
            cache = self.arm_caches.get(arm_id)
            if not cache:
                self._update_arm_status_locked(arm_id, "NO_DATA_TO_SEND")
                self._append_history_locked(
                    event="data_request_empty",
                    message="收到data请求但无缓存",
                    arm_id=arm_id,
                    level="WARN",
                )
                if not self.send_default_on_empty_request:
                    return None

                # 步骤1：填充默认值
                payload = self._default_payload_for_arm(arm_id, None)
                history_message = f"无缓存，回默认值 bytes={len(payload)}"
                self._update_arm_status_locked(arm_id, "DEFAULT_SENT_NO_CACHE")

                # 步骤2：发送PLC正常流程回复，让PLC继续发送下一个命令
                # 缓存为空表示该位置还没有进行过相机流程，发送Done让PLC继续
                if arm_id == 1:
                    self._send_plc_stage_feedback("1Done")
                elif arm_id == 2:
                    self._send_plc_stage_feedback("2Done")
            else:
                queue_item = cache.queue_item

                # data 请求不改变“出队选择”，只发送当前缓存。
                # 因此同一缓存周期内重复 data 会得到同一条数据（直到 PLC 指针变化覆盖缓存）。
                if queue_item.data is None:
                    # dequeue_timeout 模式：出队时仍无数据则立即填默认值
                    if self.timeout_mode == "dequeue_timeout":
                        # 步骤1：填充默认值
                        self._fill_default_for_slot_locked(arm_id, queue_item.pointer, "dequeue_request")

                        # 步骤2：发送PLC正常流程回复，让PLC继续发送下一个命令
                        # 超时填默认值后，发与正常相机匹配相同的握手码
                        if arm_id == 1:
                            self._send_plc_stage_feedback("1Done")
                        elif arm_id == 2:
                            self._send_plc_stage_feedback("2Done")

                if queue_item.data is None:
                    # cycle_timeout 模式且尚未触发超时时，会返回无数据
                    self._update_arm_status_locked(arm_id, "NO_DATA_TO_SEND")
                    self._append_history_locked(
                        event="data_request_empty",
                        message="收到data请求但队列项暂无数据",
                        arm_id=arm_id,
                        pointer=queue_item.pointer,
                        count=queue_item.count,
                        level="WARN",
                    )
                    return None

                cache.data_sent = True
                cache.send_count += 1
                queue_item.send_count += 1
                queue_item.status = "DATA_SENT"
                queue_item.updated_at = time.time()

                self._update_arm_status_locked(arm_id, "DATA_SENT")
                payload = queue_item.data
                history_pointer = queue_item.pointer
                history_count = queue_item.count
                history_message = f"已下发缓存数据 bytes={len(payload)}"

        self._send_plc_feedback(self._build_arm_feedback_code(arm_id, "N"))
        logger.info("[队列] 机械臂数据已取出 机械臂=%s 字节数=%s", arm_id, len(payload))
        with self.lock:
            self._append_history_locked(
                event="data_sent",
                message=history_message or f"已下发缓存数据 bytes={len(payload)}",
                arm_id=arm_id,
                pointer=history_pointer,
                count=history_count,
                history_data=self._format_history_data(payload),
            )
            # 竞态修复（防御性编程）：
            # 发送完当前缓存数据后，立即检查 PLC 最新出队指针是否已变化。
            # 若变化，则主动更新缓存（确保下一次 data 请求可以获取最新数据）。
            #
            # 背景：
            # - 原问题：PLC 同时向机械臂发 IO 信号 + 向软件发出队指针，顺序不确定
            # - 当前方案：PLC 侧已增加 800ms 延时，先发出队指针，800ms 后再发 IO 信号
            # - 此代码作为防御性编程，处理极端情况（如网络延迟波动、PLC 延时不稳定等）
            last_known_ptr = self.last_dequeue_pointers.get(arm_id)
            current_cache_ptr = self.arm_positions.get(arm_id)
            if last_known_ptr is not None and last_known_ptr != current_cache_ptr:
                logger.debug(
                    "[队列] 机械臂=%s 竞态保护：指针变化 %s→%s，刷新缓存",
                    arm_id, current_cache_ptr, last_known_ptr,
                )
                self._process_arm_dequeue_locked(arm_id, last_known_ptr)
        return payload

    def handle_arm_completion(self, arm_id: int, reported_count: Optional[int] = None) -> bool:
        """
        处理机械臂完成确认（命令 "1" 或 "1,count"）。
        
        处理流程：
            1. 查找当前机械臂的出队缓存
            2. 校验计数一致性（可选）：
               - 如果配置了计数校验，比较机械臂报告的计数与预期计数
               - 不一致只记录告警，不阻断完成流程（确保 PLC 能收到 XD）
            3. 检查计数连续性：
               - 检测是否有计数跳跃（如从5跳到7，跳过了6）
            4. 更新队列项状态：
               - confirmed: 任务已确认完成
               - dequeued: 已完成出队
               - status: TASK_COMPLETED 或 TASK_COMPLETED_WITH_COUNT_MISMATCH
            5. 发送 XD 反馈码给 PLC
            
        重要设计：
            - 计数校验失败只告警不阻断，因为机械臂可能提前收到数据就开始执行
            - 需要发送 XD 让 PLC 知道该任务已彻底完成
            
        握手协议：
            - 发送 XD 反馈码（如 1D 表示 arm1 任务完成）
            
        Args:
            arm_id: 机械臂编号（1 或 2）
            reported_count: 机械臂报告的计数值（可选，用于校验）
            
        Returns:
            bool: True 表示成功处理，False 表示无活动任务
        """
        should_count_completion = False
        mismatch = False
        with self.lock:
            cache = self.arm_caches.get(arm_id)
            if not cache:
                self._update_arm_status_locked(arm_id, "NO_ACTIVE_TASK")
                return False

            expected_count = int(cache.queue_item.count)
            last_count = self.arm_last_confirmed_count.get(arm_id)
            if (
                self.completion_count_validation
                and reported_count is not None
                and int(reported_count) != expected_count
            ):
                # 计数不一致只记告警，不阻断完成链路，确保 PLC 仍能收到 XD 收尾。
                mismatch = True
                self._append_history_locked(
                    event="count_mismatch",
                    message=f"完成计数不匹配（expect={expected_count}, got={reported_count}）",
                    arm_id=arm_id,
                    pointer=cache.queue_item.pointer,
                    count=expected_count,
                    level="WARN",
                )

            if last_count is not None and expected_count != ((last_count + 1) & 0xFFFF):
                self._append_history_locked(
                    event="count_jump",
                    message=f"完成计数不连续（last={last_count}, current={expected_count}）",
                    arm_id=arm_id,
                    pointer=cache.queue_item.pointer,
                    count=expected_count,
                    level="WARN",
                )

            if not cache.confirmed:
                cache.confirmed = True
                should_count_completion = True

            cache.queue_item.confirmed = True
            cache.queue_item.dequeued = True
            cache.queue_item.status = "TASK_COMPLETED" if not mismatch else "TASK_COMPLETED_WITH_COUNT_MISMATCH"
            cache.queue_item.updated_at = time.time()
            self._update_arm_status_locked(arm_id, "TASK_COMPLETED")
            self.arm_last_confirmed_count[arm_id] = expected_count

        if should_count_completion:
            self.stats["completed_tasks"] += 1

        self._send_plc_feedback(self._build_arm_feedback_code(arm_id, "D"))
        logger.info("[队列] 机械臂完成确认 机械臂=%s", arm_id)
        with self.lock:
            self._append_history_locked(
                event="task_done",
                message=(
                    f"收到机械臂完成确认 count={reported_count}"
                    if reported_count is not None
                    else "收到机械臂完成确认"
                ),
                arm_id=arm_id,
                pointer=cache.queue_item.pointer if cache else None,
                count=cache.queue_item.count if cache else None,
                history_data=self._format_history_data(cache.queue_item.data) if cache else "",
            )
        return True

    def _validate_pointer_consistency(self, count: int, pointer: int) -> Optional[str]:
        """目前仅保留轻量告警，不作为阻断逻辑。"""
        # 备注：真实业务是“脉冲不断增大，最小目标值先触发”，
        # 不应强耦合到 count%10+1。此处不做严格校验，只记录可选告警。
        if pointer <= 0:
            msg = f"invalid pointer count={count} pointer={pointer}"
            self.stats["validation_warnings"] += 1
            logger.warning("[队列] %s", msg)
            return msg
        return None

    def _remove_oldest_pair_locked(self):
        if not self.arm_data_queues[1]:
            return

        # 溢出时按“最老 enqueue_seq”淘汰，且 arm1/arm2 成对删除，避免双臂错位。
        oldest_pointer = min(self.arm_data_queues[1], key=lambda p: self.arm_data_queues[1][p].enqueue_seq)

        for arm_id in (1, 2):
            self.arm_data_queues[arm_id].pop(oldest_pointer, None)

        # 删除相关缓存
        for arm_id, cache in list(self.arm_caches.items()):
            if cache.queue_item.pointer == oldest_pointer:
                del self.arm_caches[arm_id]
                self._update_arm_status_locked(arm_id, "CACHE_EVICTED")

        self.stats["overflow_drops"] += 1
        logger.warning("[队列] 队列溢出，丢弃最旧数据 指针=%s", oldest_pointer)
        self._append_history_locked(
            event="overflow_drop",
            message="队列溢出，覆盖最老项",
            pointer=oldest_pointer,
            level="WARN",
        )

    def _build_arm_feedback_code(self, arm_id: int, status: str) -> str:
        """
        构建给 PLC 的反馈码。
        
        反馈码格式：{arm_id}{status}
        
        状态说明：
            - N: Normal，正常数据（data 请求的响应）
            - D: Done，任务完成（completion 请求的响应）
            
        反馈码示例：
            - 1N: arm1 正常数据
            - 1D: arm1 任务完成
            - 2N: arm2 正常数据
            - 2D: arm2 任务完成
            
        Args:
            arm_id: 机械臂编号（1-9）
            status: 状态码（N 或 D）
            
        Returns:
            str: 反馈码，如 "1N"、"2D"
        """
        if arm_id < 1 or arm_id > 9:
            raise ValueError(f"invalid arm id for PLC feedback: {arm_id}")
        if status not in ("N", "D"):
            raise ValueError(f"invalid arm feedback status: {status}")
        return f"{arm_id}{status}"

    def _update_arm_status_locked(self, arm_id: int, status: str):
        old_status = self.arm_statuses.get(arm_id)
        self.arm_statuses[arm_id] = status
        if old_status != status:
            logger.info("[队列] 机械臂状态变化 机械臂=%s %s -> %s", arm_id, old_status, status)

    def _send_plc_feedback(self, message: str):
        if not self.plc_feedback_callback:
            return
        try:
            self.plc_feedback_callback(message)
        except Exception as exc:
            logger.warning("[队列] PLC反馈回调失败：%s", exc)

    def _send_plc_stage_feedback(self, message: str):
        if not self.plc_stage_callback:
            return
        try:
            self.plc_stage_callback(message)
        except Exception as exc:
            logger.warning("[队列] PLC阶段回调失败：%s", exc)

    def get_arm_status(self, arm_id: int) -> str:
        return self.arm_statuses.get(arm_id, "UNKNOWN")

    def get_system_statistics(self) -> Dict[str, Any]:
        return {
            "queue_info": {
                "current_size": len(self.arm_data_queues[1]),
                "arm1_size": len(self.arm_data_queues[1]),
                "arm2_size": len(self.arm_data_queues[2]),
                "max_length": self.length,
                "occupied_pointers": sorted(self.arm_data_queues[1].keys()),
            },
            "arm_info": {
                "active_arms": len(self.arm_caches),
                "arm_statuses": self.arm_statuses.copy(),
                "last_pointers": self.last_dequeue_pointers.copy(),
            },
            "pending_camera_slots": [],
            "statistics": self.stats.copy(),
        }

    def get_hmi_snapshot(self) -> Dict[str, Any]:
        with self.lock:
            # main_queue：用于总览（按 pointer 同步展示 arm1/arm2 状态）
            main_queue = []
            for pointer in sorted(self.arm_data_queues[1].keys(), key=lambda p: self.arm_data_queues[1][p].enqueue_seq):
                arm1_item = self.arm_data_queues[1].get(pointer)
                arm2_item = self.arm_data_queues[2].get(pointer)

                warning = None
                if (arm1_item and arm1_item.timed_out) or (arm2_item and arm2_item.timed_out):
                    warning = "TIMEOUT_DEFAULT"

                arm_cache = {
                    1: bool(arm1_item and arm1_item.data is not None),
                    2: bool(arm2_item and arm2_item.data is not None),
                }

                main_queue.append(
                    {
                        "pointer": pointer,
                        "count": arm1_item.count if arm1_item else (arm2_item.count if arm2_item else 0),
                        "has_data": arm_cache[1] or arm_cache[2],
                        "data_size": len(arm1_item.data) if (arm1_item and arm1_item.data) else 0,
                        "status": f"A1:{arm1_item.status if arm1_item else 'NA'}|A2:{arm2_item.status if arm2_item else 'NA'}",
                        "data_preview": (
                            f"A1:{self._preview_queue_data(arm1_item.data if arm1_item else None)} | "
                            f"A2:{self._preview_queue_data(arm2_item.data if arm2_item else None)}"
                        ),
                        "warning": warning,
                        "arm_cache": arm_cache,
                        "enqueue_seq": arm1_item.enqueue_seq if arm1_item else (arm2_item.enqueue_seq if arm2_item else 0),
                    }
                )

            arm_queues: Dict[str, Any] = {}
            for arm_id in (1, 2):
                entries = []
                for item in sorted(self.arm_data_queues[arm_id].values(), key=lambda x: x.enqueue_seq):
                    entries.append(
                        {
                            "pointer": item.pointer,
                            "count": item.count,
                            "has_data": item.data is not None,
                            "status": item.status,
                            "data_preview": self._preview_queue_data(item.data),
                            "send_count": item.send_count,
                            "confirmed": item.confirmed,
                            "timed_out": item.timed_out,
                            "source": item.source,
                            "updated_at": item.updated_at,
                        }
                    )
                arm_queues[str(arm_id)] = entries

            active_cache_pointers = {
                arm_id: cache.queue_item.pointer
                for arm_id, cache in self.arm_caches.items()
            }

            # 发送缓存区详情：供 HMI 独立展示每个机械臂当前缓存
            arm_send_buffers: Dict[str, Any] = {}
            for arm_id in (1, 2):
                cache = self.arm_caches.get(arm_id)
                if cache:
                    arm_send_buffers[str(arm_id)] = {
                        "has_data": cache.queue_item.data is not None,
                        "data_preview": self._preview_queue_data(cache.queue_item.data),
                        "pointer": cache.trigger_pointer,
                        "count": cache.queue_item.count,
                        "send_count": cache.send_count,
                        "confirmed": cache.confirmed,
                        "source": cache.queue_item.source,
                    }
                else:
                    arm_send_buffers[str(arm_id)] = {
                        "has_data": False,
                        "data_preview": "-",
                        "pointer": None,
                        "count": None,
                        "send_count": 0,
                        "confirmed": False,
                        "source": "-",
                    }

            history_items = list(self.event_history)[-self.history_display_limit :]
            normalized_history_items = []
            for item in history_items:
                row = dict(item)
                row["history_data"] = self._normalize_history_data_for_hmi(row.get("history_data", ""))
                normalized_history_items.append(row)

            return {
                "main_queue": main_queue,
                "arm_queues": arm_queues,
                "arm_send_buffers": arm_send_buffers,
                "history_items": normalized_history_items,
                "history_meta": {
                    "stored": len(self.event_history),
                    "store_limit": self.history_limit,
                    "display_limit": self.history_display_limit,
                },
                "pointers": {
                    "dequeue": self.last_dequeue_pointers.copy(),
                    "arm_positions": self.arm_positions.copy(),
                    "active_cache_pointers": active_cache_pointers,
                },
                "arm_statuses": self.arm_statuses.copy(),
                "pending_camera_slots": [],
                "statistics": self.stats.copy(),
                "updated_at": time.time(),
            }
