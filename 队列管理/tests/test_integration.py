"""
增强版集成测试。

目标：
1. 验证从入队到机械臂完成的主链路闭环。
2. 验证 9009 通道反馈码发送（1N/1D/2N/2D）。
3. 验证 HMI API 能输出“分机械臂队列 + 指针”。
"""

import json
import os
import sys
import urllib.request
import unittest

# 让测试可直接从项目根目录导入 src 包
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from src.core.plc_handler import DualPortPLCHandler, PLCConnectionInfo
from src.core.queue_manager import EnhancedQueueManager
from src.web.dashboard_server import DashboardServer
from src.utils.data_parser import EnhancedDataParser


class _FakeSocket:
    """测试用假 socket，仅记录 sendall 内容。"""

    def __init__(self):
        self.sent = []

    def sendall(self, payload: bytes):
        self.sent.append(payload)

    def close(self):
        pass


class TestEnhancedWorkflow(unittest.TestCase):
    """验证核心业务流程。"""

    def setUp(self):
        # 集成测试使用固定字段顺序，避免受现场配置切换影响。
        self.parser = EnhancedDataParser(enqueue_field_order="command_count_pointer")
        self.feedbacks = []
        self.qm = EnhancedQueueManager(length=3)
        self.qm.set_callbacks(
            camera_cb=lambda _c, _p: None,
            plc_feedback_cb=lambda code: self.feedbacks.append(code),
        )

    def test_end_to_end_flow(self):
        # 1) PLC 入队帧（01）=> 双队列同步入队
        enqueue_raw = b"\x00\x01\x00\x0A\x00\x01"
        enqueue = self.parser.parse_plc_enqueue_data(enqueue_raw)
        self.qm.handle_plc_command(enqueue.command, enqueue.sensor_count, enqueue.enqueue_pointer)

        # 2) 相机数据处理并回填队列
        processed = self.parser.process_camera_data(
            sensor_count=enqueue.sensor_count,
            pulse_pointer=enqueue.enqueue_pointer,
            raw_points=[1.0, 10.2, 20.4],
        )
        packed = self.parser.pack_arm_data(processed)
        self.qm.ingest_camera_data(packed)

        # 3) PLC 出队指针触发 arm1 缓存
        self.qm.handle_dequeue_pointers(b"\x00\x01\x00\x00")
        payload = self.qm.handle_arm_data_request(1)
        # 请求数据后应该拿到非空 payload。
        self.assertIsNotNone(payload)

        # 4) 机械臂收到 payload 后可正确解包
        unpacked = self.parser.unpack_arm_data(payload)
        # 解包后的计数应与入队计数一致。
        self.assertEqual(unpacked.sensor_count, enqueue.sensor_count)
        # 解包后的指针应与入队指针一致。
        self.assertEqual(unpacked.pulse_pointer, enqueue.enqueue_pointer)

        # 5) 完成后反馈序列应为 1N -> 1D
        self.qm.handle_arm_completion(1)
        # 反馈序列必须精确符合协议时序。
        self.assertEqual(self.feedbacks, ["1N", "1D"])

        # 双队列应共享相同 pointer，保证脉冲队列与数据队列对齐。
        self.assertIn(1, self.qm.arm_data_queues[1])
        self.assertIn(1, self.qm.arm_data_queues[2])

    def test_non_concurrent_camera_data_is_assigned_in_command_order(self):
        # 非并发模式（sequential）：相机回包按挂槽顺序落入 arm1 / arm2。
        self.qm.handle_plc_command(0x01, 7, 8)     # arm1 slot
        self.qm.handle_plc_command(0x11, 999, 999) # arm2 slot（count忽略）

        self.qm.ingest_camera_data(b"A1_PAYLOAD")
        self.qm.ingest_camera_data(b"A2_PAYLOAD")

        # 第一条相机回包应写入 arm1:pointer=8
        self.assertEqual(self.qm.arm_data_queues[1][8].data, b"A1_PAYLOAD")
        # 第二条相机回包应写入 arm2:pointer=8
        self.assertEqual(self.qm.arm_data_queues[2][8].data, b"A2_PAYLOAD")

    def test_repeat_data_allowed_after_completion(self):
        # 验证“重复 data 请求”场景：完成后再次 data 仍可重发
        self.qm.enqueue_item(5, 6)
        self.qm.store_camera_data(5, 6, b"abc", arm_id=1)
        self.qm.handle_dequeue_pointers(b"\x00\x06\x00\x00")

        first = self.qm.handle_arm_data_request(1)
        self.qm.handle_arm_completion(1)
        second = self.qm.handle_arm_data_request(1)

        # 首次 data 请求应拿到缓存数据。
        self.assertEqual(first, b"abc")
        # 完成后再次 data 仍应拿到同一缓存数据。
        self.assertEqual(second, b"abc")
        # 反馈时序应为：首次 data=1N，完成=1D，再次 data=1N。
        self.assertEqual(self.feedbacks, ["1N", "1D", "1N"])

    def test_pointer_unchanged_only_triggers_once(self):
        # 同样的出队指针重复上报，不应重复创建缓存操作计数
        self.qm.enqueue_item(0, 1)
        self.qm.handle_dequeue_pointers(b"\x00\x01\x00\x00")
        first_ops = self.qm.stats["dequeue_operations"]

        self.qm.handle_dequeue_pointers(b"\x00\x01\x00\x00")
        # 指针未变化时，dequeue_operations 计数应保持不变。
        self.assertEqual(self.qm.stats["dequeue_operations"], first_ops)

    def test_overflow_drops_oldest_item(self):
        # 队列长度为3，插入4条后应覆盖最老（pointer=1）
        self.qm.enqueue_item(0, 1)
        self.qm.enqueue_item(1, 2)
        self.qm.enqueue_item(2, 3)
        self.qm.enqueue_item(3, 4)

        # 主队列容量应严格保持在 length=3。
        self.assertEqual(len(self.qm.queue), 3)
        # 最老 pointer=1 应被淘汰。
        self.assertNotIn(1, self.qm.queue)

    def test_11_and_12_do_not_create_new_queue_items(self):
        # 规则：只有 01 创建新工件，11/12 仅用于流程控制
        self.qm.handle_plc_command(0x01, 10, 2)
        size_after_01 = len(self.qm.arm_data_queues[1])
        self.qm.handle_plc_command(0x11, 999, 999)
        self.qm.handle_plc_command(0x12, 888, 888)

        # 11/12 之后队列长度不应增加
        self.assertEqual(len(self.qm.arm_data_queues[1]), size_after_01)


class TestFeedbackToPLC9009(unittest.TestCase):
    """验证反馈码通过 9009 链路发送给 PLC。"""

    def test_queue_feedback_is_sent_via_dequeue_channel(self):
        plc = DualPortPLCHandler(host="127.0.0.1")
        fake_sock = _FakeSocket()
        plc.connections["dequeue_mock"] = PLCConnectionInfo(
            connection_type="dequeue",
            socket=fake_sock,
            address=("127.0.0.1", 9009),
            connected=True,
        )

        qm = EnhancedQueueManager(length=2)
        qm.set_callbacks(
            camera_cb=lambda _c, _p: None,
            plc_feedback_cb=plc.send_dequeue_feedback,
        )

        qm.enqueue_item(0, 1)
        qm.store_camera_data(0, 1, b"hello")
        qm.handle_dequeue_pointers(b"\x00\x01\x00\x00")
        qm.handle_arm_data_request(1)
        qm.handle_arm_completion(1)

        # 9009 通道收到的反馈码顺序应是 1N 再 1D。
        self.assertEqual(fake_sock.sent, [b"1N", b"1D"])


class TestDashboardServer(unittest.TestCase):
    """验证 HMI 状态 API 输出结构。"""

    def test_api_exposes_split_queues_and_pointers(self):
        qm = EnhancedQueueManager(length=2)
        qm.set_callbacks(camera_cb=lambda _c, _p: None, plc_feedback_cb=lambda _f: None)
        qm.enqueue_item(0, 1)
        qm.store_camera_data(0, 1, b"demo")
        qm.handle_dequeue_pointers(b"\x00\x01\x00\x00")

        # 使用 port=0 让系统分配空闲端口，避免端口冲突
        server = DashboardServer(snapshot_provider=qm.get_hmi_snapshot, host="127.0.0.1", port=0)
        # dashboard 服务应启动成功。
        self.assertTrue(server.start())
        port = server._httpd.server_address[1]

        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/status", timeout=2) as resp:
                payload = json.loads(resp.read().decode("utf-8"))
        finally:
            server.stop()

        # 返回结构应包含 arm_queues 字段。
        self.assertIn("arm_queues", payload)
        # arm_queues 中应包含 arm1 视图。
        self.assertIn("1", payload["arm_queues"])
        # arm_queues 中应包含 arm2 视图。
        self.assertIn("2", payload["arm_queues"])
        # 返回结构应包含 pointers 字段。
        self.assertIn("pointers", payload)
        # pointers 中应包含 dequeue 指针区。
        self.assertIn("dequeue", payload["pointers"])
        # 历史项应包含 history_data 字段（用于展示原始数据）。
        self.assertIn("history_items", payload)
        self.assertGreater(len(payload["history_items"]), 0)
        self.assertIn("history_data", payload["history_items"][0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
