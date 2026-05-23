"""
数据解析模块（增强版）。

职责：
1. 解析 PLC 入队/出队报文。
2. 处理相机点位为机械臂可发送结构。
3. 打包/解包机械臂二进制数据。
"""

import struct
import re
from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Optional


@dataclass
class ParsedPLCEnqueueData:
    """PLC 入队报文解析结果。"""

    command: int
    sensor_count: int
    enqueue_pointer: int
    additional_data: bytes


@dataclass
class ParsedPLCDequeueData:
    """PLC 出队报文解析结果。"""

    arm_pointers: Dict[int, int]


@dataclass
class ProcessedArmData:
    """机械臂点位数据结构。"""

    flag: int
    sensor_count: int
    pulse_pointer: int
    points: List[float]


class EnhancedDataParser:
    """增强版数据解析器。"""

    def __init__(self, enqueue_field_order: Optional[str] = None):
        self.arm_count = 2
        # PLC 入队字段顺序：
        # 1) command_count_pointer：命令 + 计数 + 指针（默认历史格式）
        # 2) command_pointer_count：命令 + 指针 + 计数（部分现场PLC程序）
        self.enqueue_field_order = enqueue_field_order or self._load_enqueue_field_order_from_config()
        # 相机计数提取器注册表，方便后续按现场协议扩展新模式。
        self.camera_count_extractors: Dict[str, Callable[[bytes], Optional[int]]] = {
            "ascii": self._extract_camera_count_ascii,
            "binary": self._extract_camera_count_binary,
        }

    def _load_enqueue_field_order_from_config(self) -> str:
        """从配置读取入队字段顺序，读取失败时回退到历史默认值。"""
        try:
            from config.settings import DATA_FORMAT  # 延迟导入，避免模块初始化阶段循环依赖

            value = (
                DATA_FORMAT.get("plc_enqueue_format", {}).get("field_order", "command_count_pointer")
            )
            value = str(value).strip().lower()
            if value in ("command_count_pointer", "command_pointer_count"):
                return value
        except Exception:
            pass
        return "command_count_pointer"

    def parse_plc_enqueue_data(self, raw_data: bytes) -> ParsedPLCEnqueueData:
        """
        解析 PLC 入队报文。

        格式：command(2) + count(2) + pointer(2) = 6字节
        """
        if len(raw_data) < 6:
            raise ValueError("PLC enqueue data too short")

        command, field2, field3 = struct.unpack(">HHH", raw_data[:6])
        if self.enqueue_field_order == "command_count_pointer":
            sensor_count, enqueue_pointer = field2, field3
        elif self.enqueue_field_order == "command_pointer_count":
            enqueue_pointer, sensor_count = field2, field3
        else:
            raise ValueError(f"unsupported enqueue field order: {self.enqueue_field_order}")

        return ParsedPLCEnqueueData(
            command=command,
            sensor_count=sensor_count,
            enqueue_pointer=enqueue_pointer,
            additional_data=raw_data[6:],
        )

    def parse_plc_dequeue_data(self, raw_data: bytes) -> ParsedPLCDequeueData:
        """
        解析 PLC 出队报文。

        格式：
        arm1_pointer(2) + arm2_pointer(2)
        """
        if len(raw_data) != 4:
            raise ValueError(f"PLC dequeue data must be 4 bytes, got {len(raw_data)}")

        return ParsedPLCDequeueData(
            arm_pointers={
                1: int.from_bytes(raw_data[:2], byteorder="big"),
                2: int.from_bytes(raw_data[2:], byteorder="big"),
            }
        )

    def detect_pointer_changes(
        self,
        current_data: ParsedPLCDequeueData,
        last_data: Optional[ParsedPLCDequeueData],
    ) -> Dict[int, int]:
        """比较本次与上次出队指针，返回变化项。"""
        if last_data is None:
            return current_data.arm_pointers.copy()

        changes: Dict[int, int] = {}
        for arm_id, pointer in current_data.arm_pointers.items():
            if pointer != last_data.arm_pointers.get(arm_id, 0):
                changes[arm_id] = pointer
        return changes

    def process_camera_data(
        self,
        sensor_count: int,
        pulse_pointer: int,
        raw_points: List[float],
    ) -> ProcessedArmData:
        """将相机原始点位整理为机械臂结构。"""
        flag = int(raw_points[0]) if raw_points else 0
        points = raw_points[1:] if len(raw_points) > 1 else []

        return ProcessedArmData(
            flag=flag,
            sensor_count=sensor_count,
            pulse_pointer=pulse_pointer,
            points=points,
        )

    def pack_arm_data(self, processed_data: ProcessedArmData) -> bytes:
        """
        打包机械臂下发数据。

        格式：
        flag(2) + sensor_count(2) + pulse_pointer(2) + points(float32...) + 'E'
        """
        payload = struct.pack(
            "!HHH",
            processed_data.flag,
            processed_data.sensor_count,
            processed_data.pulse_pointer,
        )
        for point in processed_data.points:
            payload += struct.pack("!f", float(point))
        return payload + b"E"

    def unpack_arm_data(self, data: bytes) -> ProcessedArmData:
        """解包机械臂数据。"""
        if not data or data[-1:] != b"E":
            raise ValueError("arm data missing terminator E")
        if len(data) < 7:
            raise ValueError("arm data too short")

        flag, sensor_count, pulse_pointer = struct.unpack("!HHH", data[:6])
        points: List[float] = []

        point_data = data[6:-1]
        for i in range(0, len(point_data), 4):
            if i + 4 <= len(point_data):
                points.append(struct.unpack("!f", point_data[i : i + 4])[0])

        return ProcessedArmData(
            flag=flag,
            sensor_count=sensor_count,
            pulse_pointer=pulse_pointer,
            points=points,
        )

    def validate_data_format(self, data: bytes, data_type: str = "arm") -> bool:
        """快速格式校验（用于容错/预判）。"""
        if data_type == "arm":
            return len(data) >= 7 and data[-1:] == b"E"
        if data_type == "plc_enqueue":
            return len(data) >= 6
        if data_type == "plc_dequeue":
            return len(data) == 4
        return False

    def extract_command_info(self, command: int) -> Dict[str, Any]:
        """将命令码转为可读信息（用于日志或可视化）。"""
        info = {"value": command, "hex": f"{command:02X}", "type": "unknown"}

        if command == 0x01:
            info["type"] = "camera_trigger"
            info["description"] = "trigger camera"
        elif command == 0x11:
            info["type"] = "special_process"
            info["description"] = "special process"
        elif command == 0x12:
            info["type"] = "emergency_process"
            info["description"] = "emergency process"
        else:
            info["description"] = "custom command"

        return info

    def register_camera_count_extractor(
        self,
        mode: str,
        extractor: Callable[[bytes], Optional[int]],
    ):
        """
        注册自定义相机计数提取器。

        例：
        - mode="vendor_x"
        - extractor(payload) -> Optional[int]
        """
        mode_key = str(mode or "").strip().lower()
        if not mode_key:
            raise ValueError("extractor mode must not be empty")
        if not callable(extractor):
            raise ValueError("extractor must be callable")
        self.camera_count_extractors[mode_key] = extractor

    def _extract_camera_count_binary(self, payload: bytes) -> Optional[int]:
        """从二进制头部提取计数：flag(2) + count(2) + ..."""
        if not payload or len(payload) < 4:
            return None
        try:
            _, count = struct.unpack("!HH", payload[:4])
            return int(count)
        except Exception:
            return None

    def _extract_camera_count_ascii(self, payload: bytes) -> Optional[int]:
        """从 ASCII 文本提取计数：COUNT:123 / CNT=123。"""
        if not payload:
            return None
        try:
            text = payload.decode("utf-8", errors="ignore")
            match = re.search(r"\b(?:COUNT|CNT)\s*[:=]\s*(\d+)\b", text, flags=re.IGNORECASE)
            if match:
                return int(match.group(1))
        except Exception:
            pass
        return None

    def _is_mostly_printable_text(self, payload: bytes, threshold: float = 0.85) -> bool:
        """粗略判断报文是否更像文本，避免 auto 模式误按二进制取到假计数。"""
        if not payload:
            return False
        printable = sum(1 for b in payload if (32 <= b <= 126) or b in (9, 10, 13))
        return (printable / len(payload)) >= threshold

    def extract_camera_count_from_payload(self, payload: bytes, mode: str = "auto") -> Optional[int]:
        """
        尝试从相机回包中提取计数。

        mode 说明：
        1) auto：先尝试 ASCII COUNT/CNT，再在“非文本报文”上尝试二进制。
        2) ascii：仅按 ASCII COUNT/CNT 提取。
        3) binary：仅按二进制头部(flag(2)+count(2))提取。
        4) disabled：不提取计数，返回 None。
        5) 自定义：通过 register_camera_count_extractor 注册后，可直接用 mode 名称调用。
        """
        if not payload:
            return None

        mode_key = str(mode or "auto").strip().lower()
        if mode_key == "disabled":
            return None

        if mode_key not in ("auto", ""):
            extractor = self.camera_count_extractors.get(mode_key)
            if extractor:
                return extractor(payload)
            # 未知模式按 auto 回退，避免现场配置错误直接中断流程。
            mode_key = "auto"

        # auto：优先用文本规则，只有非文本报文才尝试二进制，减少误判。
        ascii_count = self._extract_camera_count_ascii(payload)
        if ascii_count is not None:
            return ascii_count

        if self._is_mostly_printable_text(payload):
            return None

        return self._extract_camera_count_binary(payload)
