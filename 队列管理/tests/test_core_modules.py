"""
核心模块单元测试（增强版接口）。

目标：
1. 保证解析器、队列、PLC处理器、机械臂控制器的关键行为不回归。
2. 用测试明确业务协议：XN/XD、覆盖最老、机械臂分队列可视化。
"""

import os
import socket
import sys
import unittest
from unittest.mock import patch

# 让测试可直接从项目根目录导入 src 包
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from src.core.arm_controller import ArmStatus, SimpleArmController
from src.core.camera_handler import AsyncCameraHandler
from src.core.enqueue_workflow import EnqueueWorkflowCoordinator
from src.core.plc_handler import DualPortPLCHandler, PLCConnectionInfo
from src.core.queue_manager import EnhancedQueueManager
from src.utils.data_parser import EnhancedDataParser, ProcessedArmData


class _FakeSocket:
    """用于替代真实 socket 的轻量假对象。"""

    def __init__(self):
        self.sent = []
        self.closed = False

    def sendall(self, payload: bytes):
        self.sent.append(payload)

    def close(self):
        self.closed = True


class TestEnhancedDataParser(unittest.TestCase):
    """验证报文解析与机械臂数据打包/解包。"""

    def setUp(self):
        # 测试中显式指定历史顺序，避免受现场配置切换影响。
        self.parser = EnhancedDataParser(enqueue_field_order="command_count_pointer")

    def test_parse_plc_enqueue_data(self):
        # 报文：command(2)+count(2)+pointer(2)+附加数据
        raw = b"\x00\x01\x12\x34\x00\x09extra"
        result = self.parser.parse_plc_enqueue_data(raw)

        # 应正确解析 PLC 命令字。
        self.assertEqual(result.command, 1)
        # 应正确解析传感器计数。
        self.assertEqual(result.sensor_count, 0x1234)
        # 应正确解析入队指针。
        self.assertEqual(result.enqueue_pointer, 9)
        # 6 字节之后的附加数据应原样保留。
        self.assertEqual(result.additional_data, b"extra")

    def test_parse_plc_enqueue_data_pointer_count_order(self):
        # 现场顺序：command + pointer + count
        parser = EnhancedDataParser(enqueue_field_order="command_pointer_count")
        raw = b"\x00\x01\x00\x09\x12\x34"
        result = parser.parse_plc_enqueue_data(raw)

        # 指针应从第二字段解析。
        self.assertEqual(result.enqueue_pointer, 9)
        # 计数应从第三字段解析。
        self.assertEqual(result.sensor_count, 0x1234)

    def test_parse_plc_enqueue_data_3d_short_frame(self):
        # 3D控制帧：13 + count(2)
        raw = b"\x13\x00\x2A"
        result = self.parser.parse_plc_enqueue_data(raw)

        self.assertEqual(result.command, 0x13)
        self.assertEqual(result.sensor_count, 42)
        self.assertEqual(result.enqueue_pointer, 0)

    def test_parse_plc_enqueue_data_3d_short_frame_decimal_alias(self):
        # 兼容十进制13/14单字节写法（0x0D/0x0E）
        raw = b"\x0D\x00\x2A"
        result = self.parser.parse_plc_enqueue_data(raw)

        self.assertEqual(result.command, 13)
        self.assertEqual(result.sensor_count, 42)

    def test_parse_plc_dequeue_and_detect_changes(self):
        # 第一次解析：两路指针都算变化
        # 第二次解析：只有 arm2 指针变化
        first = self.parser.parse_plc_dequeue_data(b"\x00\x01\x00\x02")
        second = self.parser.parse_plc_dequeue_data(b"\x00\x01\x00\x03")

        # 首帧相对“无历史”应返回 arm1/arm2 均变化。
        self.assertEqual(self.parser.detect_pointer_changes(first, None), {1: 1, 2: 2})
        # 第二帧仅 arm2 改变，应只返回 arm2。
        self.assertEqual(self.parser.detect_pointer_changes(second, first), {2: 3})

    def test_pack_unpack_arm_data(self):
        # 验证二进制序列化前后字段保持一致（浮点允许微小误差）
        original = ProcessedArmData(flag=1, sensor_count=100, pulse_pointer=5, points=[12.5, 23.7])
        packed = self.parser.pack_arm_data(original)
        unpacked = self.parser.unpack_arm_data(packed)

        # 标志位应保持一致。
        self.assertEqual(unpacked.flag, original.flag)
        # 计数应保持一致。
        self.assertEqual(unpacked.sensor_count, original.sensor_count)
        # 指针应保持一致。
        self.assertEqual(unpacked.pulse_pointer, original.pulse_pointer)
        # 点位数量应保持一致。
        self.assertEqual(len(unpacked.points), len(original.points))
        # 第一个点位值应在误差范围内一致。
        self.assertAlmostEqual(unpacked.points[0], original.points[0], places=4)
        # 第二个点位值应在误差范围内一致。
        self.assertAlmostEqual(unpacked.points[1], original.points[1], places=4)

    def test_extract_camera_count_binary_mode(self):
        payload = b"\x00\x01\x00\x2a\xff\xee"
        # binary 模式应从头部字段提取 count=42。
        self.assertEqual(self.parser.extract_camera_count_from_payload(payload, mode="binary"), 42)

    def test_extract_camera_count_ascii_mode(self):
        payload = b"CMD:11,COUNT:123"
        # ascii 模式应从 COUNT 字段提取计数。
        self.assertEqual(self.parser.extract_camera_count_from_payload(payload, mode="ascii"), 123)

    def test_extract_camera_count_auto_avoids_text_false_binary(self):
        payload = b"CMD:11,STATUS:OK"
        # auto 模式下，纯文本但无COUNT时不应回退为二进制误判。
        self.assertIsNone(self.parser.extract_camera_count_from_payload(payload, mode="auto"))

    def test_extract_camera_count_custom_extractor(self):
        self.parser.register_camera_count_extractor(
            "vendor_x",
            lambda payload: 77 if payload.startswith(b"VX|") else None,
        )
        payload = b"VX|COUNT?ignored"
        # 自定义提取器应可被直接按 mode 调用。
        self.assertEqual(self.parser.extract_camera_count_from_payload(payload, mode="vendor_x"), 77)


class TestEnhancedQueueManager(unittest.TestCase):
    """验证队列状态机、反馈协议与 HMI 快照结构。"""

    def setUp(self):
        self.feedbacks = []
        self.stage_messages = []
        self.camera_triggers = []
        self.qm = EnhancedQueueManager(length=2)
        self.qm.set_callbacks(
            camera_cb=lambda c, p: self.camera_triggers.append((c, p)),
            plc_feedback_cb=lambda msg: self.feedbacks.append(msg),
            plc_stage_cb=lambda msg: self.stage_messages.append(msg),
        )

    def test_overflow_drops_oldest(self):
        # length=2，连续入3条，最早的 pointer=1 应被覆盖
        self.qm.enqueue_item(0, 1)
        self.qm.enqueue_item(1, 2)
        self.qm.enqueue_item(2, 3)

        # 最老项 pointer=1 应从主队列移除。
        self.assertNotIn(1, self.qm.queue)
        # 后续项 pointer=2 应仍存在。
        self.assertIn(2, self.qm.queue)
        # 最新项 pointer=3 应入队成功。
        self.assertIn(3, self.qm.queue)
        # 溢出计数应增加 1。
        self.assertEqual(self.qm.stats["overflow_drops"], 1)

    def test_command_01_creates_two_synced_queue_items(self):
        # 业务规则：01 才创建新工件，且 arm1/arm2 两个队列同步入队。
        ok = self.qm.handle_plc_command(command=0x01, sensor_count=3, pulse_pointer=4)

        # 01 命令处理应成功。
        self.assertTrue(ok)
        # arm1 队列应包含 pointer=4。
        self.assertIn(4, self.qm.arm_data_queues[1])
        # arm2 队列也应包含相同 pointer=4，保证双队列对齐。
        self.assertIn(4, self.qm.arm_data_queues[2])
        # 01 仅挂 arm1 的相机待匹配槽位。
        self.assertEqual(len(self.qm.pending_camera_slots), 1)
        self.assertEqual(self.qm.pending_camera_slots[0]["arm_id"], 1)

    def test_command_11_ignores_plc_count_and_binds_latest_01(self):
        # 先建立有效 01 上下文（count=30,pointer=4）
        self.qm.handle_plc_command(command=0x01, sensor_count=30, pulse_pointer=4)
        # 11 的 PLC 计数不可信，故意传入无关值
        self.qm.handle_plc_command(command=0x11, sensor_count=999, pulse_pointer=999)

        # arm2 槽位应绑定到最近有效 01 的 count/pointer，而不是 11 自带值。
        arm2_slots = [slot for slot in self.qm.pending_camera_slots if slot["arm_id"] == 2]
        self.assertEqual(len(arm2_slots), 1)
        self.assertEqual(arm2_slots[0]["count"], 30)
        self.assertEqual(arm2_slots[0]["pointer"], 4)

    def test_cycle_timeout_fills_default_on_next_01(self):
        # cycle_timeout 规则：下一次 01 到来时，上一轮未收到相机数据则填默认值。
        self.qm.handle_plc_command(command=0x01, sensor_count=3, pulse_pointer=4)    # arm1 pending
        self.qm.handle_plc_command(command=0x11, sensor_count=3, pulse_pointer=4)    # arm2 pending
        self.qm.handle_plc_command(command=0x01, sensor_count=4, pulse_pointer=5)    # 触发上一轮超时填充

        arm1_item = self.qm.arm_data_queues[1][4]
        arm2_item = self.qm.arm_data_queues[2][4]
        # arm1 超时后应填入默认值 (1000,count)E。
        self.assertEqual(arm1_item.data, b"(1000,3)E")
        # arm2 超时后应填入默认值 (2000,count)E。
        self.assertEqual(arm2_item.data, b"(2000,3)E")
        # 两个队列项都应标记为超时。
        self.assertTrue(arm1_item.timed_out)
        self.assertTrue(arm2_item.timed_out)

    def test_cycle_timeout_fills_arm2_default_even_without_11_slot(self):
        # 仅收到01，未收到11时，arm2本轮无pending槽位；
        # 下一次01到来时，arm2也应补默认值，避免长期空数据。
        self.qm.handle_plc_command(command=0x01, sensor_count=3, pulse_pointer=4)
        self.qm.handle_plc_command(command=0x01, sensor_count=4, pulse_pointer=5)

        arm1_item = self.qm.arm_data_queues[1][4]
        arm2_item = self.qm.arm_data_queues[2][4]
        self.assertEqual(arm1_item.data, b"(1000,3)E")
        self.assertEqual(arm2_item.data, b"(2000,3)E")
        self.assertTrue(arm1_item.timed_out)
        self.assertTrue(arm2_item.timed_out)

    def test_dequeue_timeout_fills_default_on_data_request(self):
        # dequeue_timeout 规则：元素“应出队”时仍无相机数据，立即填默认值。
        qm = EnhancedQueueManager(length=2, timeout_mode="dequeue_timeout")
        qm.enqueue_item(8, 9)
        qm.handle_dequeue_pointers(b"\x00\x09\x00\x00")

        # 指针到达后（即应出队时）应立即填默认，不需等 data 请求。
        self.assertEqual(qm.arm_data_queues[1][9].data, b"(1000,8)E")

        payload = qm.handle_arm_data_request(1)

        # arm1 在无相机数据时应返回默认值。
        self.assertEqual(payload, b"(1000,8)E")
        # 队列项应被标记为超时填充。
        self.assertTrue(qm.arm_data_queues[1][9].timed_out)

    def test_late_camera_data_is_dropped_after_timeout_fill(self):
        # 超时后晚到数据应丢弃，防止覆盖默认值导致队列错位。
        self.qm.handle_plc_command(command=0x01, sensor_count=3, pulse_pointer=4)
        self.qm.handle_plc_command(command=0x01, sensor_count=4, pulse_pointer=5)  # 触发 pointer=4 超时
        ok = self.qm.store_camera_data(sensor_count=3, pulse_pointer=4, camera_data=b"late", arm_id=1)

        # 晚到数据不应覆盖已超时项。
        self.assertFalse(ok)

    def test_arm_feedback_protocol_and_repeat_data(self):
        # data 请求允许重复下发，同步反馈 1N；完成后反馈 1D
        self.qm.enqueue_item(10, 1)
        self.qm.store_camera_data(10, 1, b"payload")
        self.qm.handle_dequeue_pointers(b"\x00\x01\x00\x00")

        first = self.qm.handle_arm_data_request(1)
        second = self.qm.handle_arm_data_request(1)

        # 第一次 data 请求应收到 payload。
        self.assertEqual(first, b"payload")
        # 重复 data 请求应继续收到同一 payload（可重复发送）。
        self.assertEqual(second, b"payload")
        # 两次 data 请求应产生两次 1N 反馈。
        self.assertEqual(self.feedbacks, ["1N", "1N"])

        ok = self.qm.handle_arm_completion(1)
        # 完成请求应被系统接受。
        self.assertTrue(ok)
        # 完成后最后一条反馈应为 1D。
        self.assertEqual(self.feedbacks[-1], "1D")

    def test_invalid_pointer_sets_status(self):
        # 当 PLC 指针指向不存在项时，状态应进入 NO_DATA_AVAILABLE
        self.qm.handle_dequeue_pointers(b"\x03\xE7\x00\x00")
        # arm1 状态应标记为无可用数据。
        self.assertEqual(self.qm.get_arm_status(1), "NO_DATA_AVAILABLE")

    def test_empty_data_request_can_send_default_when_enabled(self):
        feedbacks = []
        qm = EnhancedQueueManager(length=2, send_default_on_empty_request=True)
        qm.set_callbacks(
            camera_cb=lambda _c, _p: None,
            plc_feedback_cb=lambda code: feedbacks.append(code),
        )

        payload = qm.handle_arm_data_request(1)

        # 无缓存时应返回机械臂默认值，不再静默。
        self.assertEqual(payload, b"(1000,0)E")
        # 发送默认值后仍应反馈“已发送未完成”。
        self.assertEqual(feedbacks, ["1N"])

    def test_hmi_snapshot_has_split_arm_queues_and_pointers(self):
        # HMI 快照必须包含：
        # 1) arm_queues 分臂数据（至少键"1"和"2"）
        # 2) pointers.dequeue 中可见 arm1/arm2 指针
        self.qm.enqueue_item(0, 1)
        self.qm.enqueue_item(1, 2)
        self.qm.store_camera_data(0, 1, b"a")
        self.qm.store_camera_data(1, 2, b"b")
        self.qm.handle_dequeue_pointers(b"\x00\x01\x00\x02")
        self.qm.handle_arm_data_request(1)
        self.qm.handle_arm_data_request(2)

        snapshot = self.qm.get_hmi_snapshot()
        # 快照中必须包含 arm1 分队列。
        self.assertIn("1", snapshot["arm_queues"])
        # 快照中必须包含 arm2 分队列。
        self.assertIn("2", snapshot["arm_queues"])
        # 指针区应显示 arm1 当前出队指针为 1。
        self.assertEqual(snapshot["pointers"]["dequeue"][1], 1)
        # 指针区应显示 arm2 当前出队指针为 2。
        self.assertEqual(snapshot["pointers"]["dequeue"][2], 2)
        # arm1 分队列中应出现 pointer=1 的条目。
        self.assertTrue(any(item["pointer"] == 1 for item in snapshot["arm_queues"]["1"]))
        # arm2 分队列中应出现 pointer=2 的条目。
        self.assertTrue(any(item["pointer"] == 2 for item in snapshot["arm_queues"]["2"]))
        # 分队列项应提供队列数据预览字段。
        self.assertIn("data_preview", snapshot["arm_queues"]["1"][0])
        # 主队列项应提供双臂数据预览字段。
        self.assertIn("data_preview", snapshot["main_queue"][0])

    def test_camera_match_triggers_stage_handshake_messages(self):
        # 相机1数据到达 -> 回1Done；相机2数据到达 -> 回2Done
        self.qm.handle_plc_command(command=0x01, sensor_count=3, pulse_pointer=4)
        self.qm.ingest_camera_data(b"a1")
        self.qm.handle_plc_command(command=0x11, sensor_count=999, pulse_pointer=999)
        self.qm.ingest_camera_data(b"a2")

        self.assertEqual(self.stage_messages, ["1Done", "2Done"])

    def test_command_12_is_forwarded_to_camera(self):
        # 先建立有效 01 上下文，确保12绑定最近01计数
        self.qm.handle_plc_command(command=0x01, sensor_count=30, pulse_pointer=4)
        # 触发12：应透传给相机，不创建新队列项
        ok = self.qm.handle_plc_command(command=0x12, sensor_count=999, pulse_pointer=999)

        self.assertTrue(ok)
        # 预期相机回调收到(12, 最近01计数30)
        self.assertIn((0x12, 30), self.camera_triggers)

    def test_decimal_13_is_normalized_to_0x13_for_camera(self):
        ok = self.qm.handle_plc_command(command=13, sensor_count=66, pulse_pointer=0)
        self.assertTrue(ok)
        self.assertIn((0x13, 66), self.camera_triggers)

    def test_decimal_11_is_normalized_to_flow_command(self):
        # 兼容现场PLC十进制写法：11 应按流程命令 0x11 处理
        self.qm.handle_plc_command(command=1, sensor_count=30, pulse_pointer=4)
        ok = self.qm.handle_plc_command(command=11, sensor_count=999, pulse_pointer=999)

        self.assertTrue(ok)
        arm2_slots = [slot for slot in self.qm.pending_camera_slots if slot["arm_id"] == 2]
        self.assertEqual(len(arm2_slots), 1)
        self.assertEqual(arm2_slots[0]["count"], 30)
        self.assertEqual(arm2_slots[0]["pointer"], 4)
        # 对相机下发应是流程命令0x11，而不是0x0B
        self.assertIn((0x11, 30), self.camera_triggers)

    def test_decimal_12_is_normalized_to_flow_command(self):
        # 兼容现场PLC十进制写法：12 应按流程命令 0x12 透传
        self.qm.handle_plc_command(command=1, sensor_count=31, pulse_pointer=5)
        ok = self.qm.handle_plc_command(command=12, sensor_count=999, pulse_pointer=999)

        self.assertTrue(ok)
        # 对相机下发应是流程命令0x12，而不是0x0C
        self.assertIn((0x12, 31), self.camera_triggers)

    def test_history_data_is_human_readable_for_arm_payload(self):
        parser = EnhancedDataParser(enqueue_field_order="command_count_pointer")
        self.qm.handle_plc_command(command=0x01, sensor_count=10, pulse_pointer=2)
        arm_payload = parser.pack_arm_data(
            ProcessedArmData(flag=1, sensor_count=10, pulse_pointer=2, points=[12.5, 23.75])
        )
        self.qm.ingest_camera_data(arm_payload)

        snapshot = self.qm.get_hmi_snapshot()
        history_items = snapshot.get("history_items", [])
        camera_match = next((i for i in reversed(history_items) if i.get("event") == "camera_match"), None)

        self.assertIsNotNone(camera_match)
        self.assertIn("机械臂数据", camera_match.get("history_data", ""))
        self.assertIn("count=10", camera_match.get("history_data", ""))
        self.assertIn("pointer=2", camera_match.get("history_data", ""))

    def test_dequeue_cache_history_contains_cached_payload(self):
        parser = EnhancedDataParser(enqueue_field_order="command_count_pointer")
        self.qm.handle_plc_command(command=0x01, sensor_count=11, pulse_pointer=3)
        arm_payload = parser.pack_arm_data(
            ProcessedArmData(flag=2, sensor_count=11, pulse_pointer=3, points=[1.25])
        )
        self.qm.ingest_camera_data(arm_payload)
        self.qm.handle_dequeue_pointers(b"\x00\x03\x00\x00")

        snapshot = self.qm.get_hmi_snapshot()
        history_items = snapshot.get("history_items", [])
        dequeue_cache = next((i for i in reversed(history_items) if i.get("event") == "dequeue_cache"), None)

        self.assertIsNotNone(dequeue_cache)
        self.assertIn("机械臂数据", dequeue_cache.get("history_data", ""))


class TestDualPortPLCHandler(unittest.TestCase):
    """验证 PLC 双端口处理器的关键行为。"""

    def setUp(self):
        self.handler = DualPortPLCHandler(host="127.0.0.1")

    def test_feedback_send_to_dequeue_connections(self):
        # 反馈必须走 dequeue 通道
        fake_sock = _FakeSocket()
        self.handler.connections["dequeue_a"] = PLCConnectionInfo(
            connection_type="dequeue",
            socket=fake_sock,
            address=("127.0.0.1", 9009),
            connected=True,
        )

        sent = self.handler.send_dequeue_feedback("1N")
        # 存在可用 dequeue 连接时，发送结果应为成功。
        self.assertTrue(sent)
        # 实际发送内容应与反馈码完全一致。
        self.assertEqual(fake_sock.sent, [b"1N"])

    def test_pointer_change_detection(self):
        # 同样数据重复上报时，第二次不应产生变化项
        changes = self.handler._parse_and_detect_dequeue_changes(b"\x00\x01\x00\x02")
        # 首次上报应识别 arm1/arm2 都发生变化。
        self.assertEqual(changes, {1: 1, 2: 2})

        changes = self.handler._parse_and_detect_dequeue_changes(b"\x00\x01\x00\x02")
        # 第二次相同上报应返回空变化集。
        self.assertEqual(changes, {})

    def test_stage_message_send_to_enqueue_connections(self):
        # 入队阶段握手应走 enqueue 通道（例如"11"/"21"）
        fake_sock = _FakeSocket()
        self.handler.connections["enqueue_a"] = PLCConnectionInfo(
            connection_type="enqueue",
            socket=fake_sock,
            address=("127.0.0.1", 9999),
            connected=True,
        )

        sent = self.handler.send_enqueue_feedback("11")
        self.assertTrue(sent)
        self.assertEqual(fake_sock.sent, [b"11"])

    def test_recv_enqueue_frame_supports_3d_short_frame(self):
        class _SeqSocket:
            def __init__(self, payload: bytes):
                self.payload = payload
                self.idx = 0

            def recv(self, size: int):
                if self.idx >= len(self.payload):
                    return b""
                chunk = self.payload[self.idx : self.idx + size]
                self.idx += len(chunk)
                return chunk

        sock = _SeqSocket(b"\x13\x00\x2A")
        frame = self.handler._recv_enqueue_frame(sock)
        self.assertEqual(frame, b"\x13\x00\x2A")

    def test_recv_enqueue_frame_supports_3d_decimal_alias_short_frame(self):
        class _SeqSocket:
            def __init__(self, payload: bytes):
                self.payload = payload
                self.idx = 0

            def recv(self, size: int):
                if self.idx >= len(self.payload):
                    return b""
                chunk = self.payload[self.idx : self.idx + size]
                self.idx += len(chunk)
                return chunk

        sock = _SeqSocket(b"\x0D\x00\x2A")
        frame = self.handler._recv_enqueue_frame(sock)
        self.assertEqual(frame, b"\x0D\x00\x2A")

    def test_recv_enqueue_frame_keeps_legacy_6bytes(self):
        class _SeqSocket:
            def __init__(self, payload: bytes):
                self.payload = payload
                self.idx = 0

            def recv(self, size: int):
                if self.idx >= len(self.payload):
                    return b""
                chunk = self.payload[self.idx : self.idx + size]
                self.idx += len(chunk)
                return chunk

        sock = _SeqSocket(b"\x00\x01\x00\x05\x00\x09")
        frame = self.handler._recv_enqueue_frame(sock)
        self.assertEqual(frame, b"\x00\x01\x00\x05\x00\x09")


class TestArmController(unittest.TestCase):
    """验证机械臂控制器配置兼容和连接流程。"""

    def test_init_accepts_dict_config(self):
        controller = SimpleArmController({1: {"host": "127.0.0.1", "port": 6001}})
        # 应正确创建 arm1 条目。
        self.assertIn(1, controller.arms)
        # arm1 的 host 字段应正确映射。
        self.assertEqual(controller.arms[1].host, "127.0.0.1")

    def test_extract_commands_supports_sticky_packets(self):
        controller = SimpleArmController({1: {"host": "127.0.0.1", "port": 6001}})
        # 一次recv里可能粘连多条命令（含不同分隔形式）
        raw = "data\\r\\n1,12data1:13 1 14"
        cmds = controller._extract_commands(raw)
        self.assertEqual(cmds, ["data", "1,12", "data", "1:13", "1 14"])

    @patch.object(SimpleArmController, "_connect_arm", return_value=True)
    @patch.object(SimpleArmController, "_start_receiver")
    def test_connect_all(self, _mock_start_receiver, _mock_connect_arm):
        controller = SimpleArmController(
            {
                1: {"host": "127.0.0.1", "port": 6001},
                2: {"host": "127.0.0.1", "port": 6002},
            }
        )
        result = controller.connect_all()
        # 两个机械臂都连接成功时，整体 success 应为 True。
        self.assertTrue(result["success"])
        # 连接成功数量应等于 2。
        self.assertEqual(result["connected_count"], 2)

    @patch.object(SimpleArmController, "_start_receiver")
    def test_stale_connection_is_replaced_on_duplicate_connect(self, _mock_start_receiver):
        class _DeadSocket:
            def __init__(self):
                self.closed = False
                self._timeout = None

            def recv(self, _size, _flags=0):
                return b""

            def close(self):
                self.closed = True

            def settimeout(self, value):
                self._timeout = value

            def gettimeout(self):
                return self._timeout

        class _ClientSocket:
            def __init__(self):
                self.closed = False
                self._timeout = None

            def close(self):
                self.closed = True

            def setsockopt(self, *_args):
                return None

            def settimeout(self, value):
                self._timeout = value

        controller = SimpleArmController({1: {"host": "127.0.0.1", "port": 6001}})
        arm = controller.arms[1]
        old_sock = _DeadSocket()
        new_sock = _ClientSocket()
        arm.sock = old_sock
        arm.status = ArmStatus.CONNECTED
        arm.client_addr = ("127.0.0.1", 5000)

        controller._attach_passive_connection(arm, new_sock, ("127.0.0.1", 5001))

        # 旧连接假活时应由新连接接管。
        self.assertIs(arm.sock, new_sock)
        self.assertTrue(old_sock.closed)
        self.assertFalse(new_sock.closed)

    @patch.object(SimpleArmController, "_start_receiver")
    def test_alive_connection_keeps_old_on_duplicate_connect(self, _mock_start_receiver):
        class _AliveSocket:
            def __init__(self):
                self.closed = False
                self._timeout = None

            def recv(self, _size, _flags=0):
                raise BlockingIOError()

            def close(self):
                self.closed = True

            def settimeout(self, value):
                self._timeout = value

            def gettimeout(self):
                return self._timeout

        class _ClientSocket:
            def __init__(self):
                self.closed = False

            def close(self):
                self.closed = True

            def setsockopt(self, *_args):
                return None

            def settimeout(self, _value):
                return None

        controller = SimpleArmController({1: {"host": "127.0.0.1", "port": 6001}})
        arm = controller.arms[1]
        old_sock = _AliveSocket()
        new_sock = _ClientSocket()
        arm.sock = old_sock
        arm.status = ArmStatus.CONNECTED
        arm.client_addr = ("127.0.0.1", 5000)

        controller._attach_passive_connection(arm, new_sock, ("127.0.0.1", 5001))

        # 旧连接仍可用时，重复建连应丢弃新连接。
        self.assertIs(arm.sock, old_sock)
        self.assertFalse(old_sock.closed)
        self.assertTrue(new_sock.closed)


class TestCameraHandler(unittest.TestCase):
    """验证相机处理器基础初始化状态。"""

    def test_camera_initialization(self):
        handler = AsyncCameraHandler("127.0.0.1", 8080)
        # host 参数应被正确保存。
        self.assertEqual(handler.host, "127.0.0.1")
        # port 参数应被正确保存。
        self.assertEqual(handler.port, 8080)
        # 初始化时不应默认处于连接状态。
        self.assertFalse(handler.connected)

    def test_trigger_capture_binary_mode_uses_16bit_frame(self):
        class _CaptureSocket:
            def __init__(self):
                self.last = None

            def send(self, payload: bytes):
                self.last = payload

        handler = AsyncCameraHandler("127.0.0.1", 8080)
        handler.connected = True
        handler.socket = _CaptureSocket()
        ok = handler.trigger_capture(command=0x01, sensor_count=None, concurrent_mode=False)

        # 非并发模式下应发送16位二进制命令帧（0x0001）。
        self.assertTrue(ok)
        self.assertEqual(handler.socket.last, b"\x00\x01")

    def test_trigger_capture_ascii_mode_uses_cmd_count_text(self):
        class _CaptureSocket:
            def __init__(self):
                self.last = None

            def send(self, payload: bytes):
                self.last = payload

        handler = AsyncCameraHandler("127.0.0.1", 8080)
        handler.connected = True
        handler.socket = _CaptureSocket()
        ok = handler.trigger_capture(command=0x11, sensor_count=123, concurrent_mode=True)

        # 并发模式下应发送 ASCII 文本（含命令与计数）。
        self.assertTrue(ok)
        self.assertEqual(handler.socket.last, b"CMD:11,COUNT:123")

    def test_trigger_capture_3d_command_forces_ascii_with_count(self):
        class _CaptureSocket:
            def __init__(self):
                self.last = None

            def send(self, payload: bytes):
                self.last = payload

        handler = AsyncCameraHandler("127.0.0.1", 8080)
        handler.connected = True
        handler.socket = _CaptureSocket()
        ok = handler.trigger_capture(command=0x13, sensor_count=321, concurrent_mode=False)

        # 13/14 命令应强制按 ASCII 发送，便于3D并发关联 count。
        self.assertTrue(ok)
        self.assertEqual(handler.socket.last, b"CMD:13,COUNT:321")

    def test_receive_loop_continues_after_disconnect_reconnect(self):
        class _SeqSocket:
            def __init__(self, seq):
                self.seq = list(seq)

            def settimeout(self, _timeout):
                return None

            def recv(self, _size):
                if not self.seq:
                    raise socket.timeout()
                item = self.seq.pop(0)
                if isinstance(item, Exception):
                    raise item
                return item

            def close(self):
                return None

        handler = AsyncCameraHandler("127.0.0.1", 8080)
        handler.receiving = True
        handler.connected = True
        # 首次 recv 返回空包，模拟相机主动断开。
        handler.socket = _SeqSocket([b""])

        # 重连后的新 socket 提供一条正常数据。
        reconnect_socket = _SeqSocket([b"OK"])

        def _fake_disconnect():
            handler.connected = True
            handler.socket = reconnect_socket

        received = []

        def _cb(camera_data):
            received.append(camera_data.raw_data)
            handler.receiving = False
            handler._stop_event.set()

        handler.set_data_callback(_cb)

        with patch.object(handler, "_handle_disconnect", side_effect=_fake_disconnect):
            handler._receive_loop()

        # 断线重连后，应继续接收到后续数据。
        self.assertEqual(received, [b"OK"])


class TestEnqueueWorkflowCoordinator(unittest.TestCase):
    class _FakeCameraServer:
        def __init__(self):
            self.sent = []

        def send_to_camera(self, camera_key: str, payload: bytes) -> bool:
            self.sent.append((camera_key, payload))
            return True

    class _FakePLC:
        def __init__(self):
            self.enqueue_feedback = []

        def send_enqueue_feedback(self, message: str) -> bool:
            self.enqueue_feedback.append(message)
            return True

    def test_new_flow_11_2d_12_3d_done(self):
        qm = EnhancedQueueManager(length=4, timeout_mode="dequeue_timeout")
        camera = self._FakeCameraServer()
        plc = self._FakePLC()
        flow = EnqueueWorkflowCoordinator(queue_manager=qm, plc_handler=plc, camera_server=camera)

        flow.handle_plc_enqueue(command=11, sensor_count=100, pulse_pointer=7)
        self.assertIn(("3d", b"11,100"), camera.sent)
        self.assertNotIn(("2d", b"(100)E"), camera.sent)
        self.assertIn(7, qm.arm_data_queues[1])
        self.assertIn(7, qm.arm_data_queues[2])

        flow.on_camera_payload("2d", b"(READY)E")
        self.assertIn(("2d", b"(100)E"), camera.sent)

        flow.on_camera_payload("2d", b"100,TYPE_A,TYPE_B")
        self.assertIn(("2d", b"OK"), camera.sent)

        flow.handle_plc_enqueue(command=12, sensor_count=100, pulse_pointer=7)
        self.assertIn(("3d", b"12,100,TYPE_A,TYPE_B"), camera.sent)

        flow.on_camera_payload("3d", b"(1000,100,1.1)EA(2000,100,2.2)E")
        self.assertEqual(qm.arm_data_queues[1][7].data, b"(1000,100,1.1)E")
        self.assertEqual(qm.arm_data_queues[2][7].data, b"(2000,100,2.2)E")
        self.assertIn("Done", plc.enqueue_feedback)

    def test_end_without_2d_type_falls_back_immediately(self):
        qm = EnhancedQueueManager(length=4, timeout_mode="dequeue_timeout")
        camera = self._FakeCameraServer()
        plc = self._FakePLC()
        flow = EnqueueWorkflowCoordinator(queue_manager=qm, plc_handler=plc, camera_server=camera)

        flow.handle_plc_enqueue(command=11, sensor_count=101, pulse_pointer=8)
        flow.handle_plc_enqueue(command=12, sensor_count=101, pulse_pointer=8)
        self.assertIn(("3d", b"12,101,00,00"), camera.sent)

        sent_before = len(camera.sent)
        flow.on_camera_payload("2d", b"101,TYPE_B,TYPE_C")
        self.assertEqual(sent_before + 1, len(camera.sent))  # 仅有回OK
        self.assertEqual(camera.sent[-1], ("2d", b"OK"))

    def test_ready_outside_11_12_window_is_dropped(self):
        qm = EnhancedQueueManager(length=4, timeout_mode="dequeue_timeout")
        camera = self._FakeCameraServer()
        plc = self._FakePLC()
        flow = EnqueueWorkflowCoordinator(queue_manager=qm, plc_handler=plc, camera_server=camera)

        flow.on_camera_payload("2d", b"READY")
        self.assertNotIn(("2d", b"(102)E"), camera.sent)

        flow.handle_plc_enqueue(command=11, sensor_count=102, pulse_pointer=9)
        flow.handle_plc_enqueue(command=12, sensor_count=102, pulse_pointer=9)
        flow.on_camera_payload("2d", b"(READY)E")
        self.assertNotIn(("2d", b"(102)E"), camera.sent)

    def test_unknown_enqueue_command_ignored_by_default(self):
        qm = EnhancedQueueManager(length=4, timeout_mode="dequeue_timeout")
        camera = self._FakeCameraServer()
        plc = self._FakePLC()
        flow = EnqueueWorkflowCoordinator(queue_manager=qm, plc_handler=plc, camera_server=camera)

        flow.handle_plc_enqueue(command=0x01, sensor_count=200, pulse_pointer=9)
        self.assertNotIn(9, qm.arm_data_queues[1])
        self.assertNotIn(9, qm.arm_data_queues[2])

    def test_unknown_enqueue_command_can_fallback_legacy(self):
        qm = EnhancedQueueManager(length=4, timeout_mode="dequeue_timeout")
        camera = self._FakeCameraServer()
        plc = self._FakePLC()
        flow = EnqueueWorkflowCoordinator(
            queue_manager=qm,
            plc_handler=plc,
            camera_server=camera,
            unknown_enqueue_policy="legacy",
        )

        flow.handle_plc_enqueue(command=0x01, sensor_count=201, pulse_pointer=10)
        self.assertIn(10, qm.arm_data_queues[1])
        self.assertIn(10, qm.arm_data_queues[2])


if __name__ == "__main__":
    unittest.main(verbosity=2)
