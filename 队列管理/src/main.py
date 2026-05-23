"""
主程序入口（增强版）。

模块职责：
1. 初始化所有子模块（PLC、相机、机械臂、队列、解析器、HMI）。
2. 将各子模块通过回调函数连接为完整业务链路。
3. 管理系统启动、监控与优雅关闭。

日志配置说明：
    日志配置位于 config/settings.py 的 LOGGING_CONFIG 中：
    - log_mode: simple(默认) 或 detailed
        simple:   单一日志文件，不记录原始通讯内容
        detailed: 多个日志文件(enqueue.log/dequeue.log/system.log)，记录原始通讯内容
    - timestamp_ms: 是否显示毫秒时间戳
"""

# ===== 必须在最开头设置 sys.path =====
import sys
from pathlib import Path

# 兼容 pyinstaller 打包后的 exe 运行
if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
    # exe 运行时的根目录（_internal 的父目录）
    APP_ROOT = Path(sys._MEIPASS).parent
else:
    APP_ROOT = Path(__file__).resolve().parents[1]

# 将 src 目录加入 sys.path，确保 "from utils.xxx" 和 "from src.xxx" 都能工作
SRC_DIR = Path(__file__).resolve().parent
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

# 将项目根目录也加入 sys.path
if str(APP_ROOT) not in sys.path:
    sys.path.insert(0, str(APP_ROOT))

import logging
import os
import re
import signal
import time
from typing import Any, Dict

# 导入日志工具模块（在 sys.path 设置之后）
from src.utils.logger import SystemLogger

from config.settings import (
    ARM_TCP_CONFIG,
    CAMERA_TCP_CONFIG,
    CAMERA_SERVER_CONFIG,
    HMI_CONFIG,
    LOGGING_CONFIG,
    PLC_TCP_CONFIG,
    QUEUE_RUNTIME_CONFIG,
    QUEUE_CONFIG,
    SYSTEM_CONFIG,
)
from src.core.arm_controller import SimpleArmController
from src.core.camera_server import DualCameraServer
from src.core.enqueue_workflow import EnqueueWorkflowCoordinator
from src.core.plc_handler import DualPortPLCHandler
from src.core.queue_manager import EnhancedQueueManager
from src.web.dashboard_server import DashboardServer
from src.utils.data_parser import EnhancedDataParser


# 启动前确保日志目录存在，避免 FileHandler 因路径不存在抛异常。
# 获取程序根目录（支持 pyinstaller 打包后的 exe 运行）
if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
    APP_ROOT = Path(sys._MEIPASS)
else:
    APP_ROOT = Path(__file__).resolve().parents[1]

log_file_path = LOGGING_CONFIG["log_file"]
if not os.path.isabs(log_file_path):
    log_file_path = os.path.join(APP_ROOT, log_file_path)
_log_dir = os.path.dirname(log_file_path)
if _log_dir:
    os.makedirs(_log_dir, exist_ok=True)

# 使用统一的日志系统（支持分文件、毫秒时间戳等配置）
# 日志配置由 config/settings.py 中的 LOGGING_CONFIG 控制
# 详细说明见文件开头的注释
SystemLogger()
logger = logging.getLogger(__name__)


class EnhancedIndustrialSystem:
    """工业自动化系统总控制器。"""

    def __init__(self):
        # system_running：主循环开关
        # modules：统一保存子模块实例，便于集中管理
        # last_dequeue_data：上一次 PLC 出队指针快照，用于变化检测
        self.system_running = False
        self.modules: Dict[str, Any] = {}
        self.last_dequeue_data = None
        self.enqueue_flow_mode = str(
            QUEUE_RUNTIME_CONFIG.get("enqueue_flow_mode", "dual_camera_11_12")
        ).strip().lower()
        self.unknown_enqueue_policy = str(
            QUEUE_RUNTIME_CONFIG.get("unknown_enqueue_policy", "ignore")
        ).strip().lower()
        # 单相机客户端模式下使用的重连节流参数。
        self._next_camera_retry_ts = 0.0
        self._camera_retry_interval = 2.0
        self.setup_signal_handlers()

    def setup_signal_handlers(self):
        """注册系统信号处理。"""
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

    def _signal_handler(self, signum, frame):
        """收到系统信号时，走统一关闭流程。"""
        logger.info("[主程序] 收到系统信号=%s，开始关闭", signum)
        self.shutdown()
        sys.exit(0)

    def initialize_modules(self) -> bool:
        """初始化所有核心模块。"""
        try:
            # 1) 数据解析器：解析 PLC/机械臂数据结构
            self.modules["data_parser"] = EnhancedDataParser()

            # 2) 队列管理器：入队、出队缓存、反馈协议的核心状态机
            self.modules["queue_manager"] = EnhancedQueueManager(
                length=QUEUE_CONFIG["length"],
                timeout_mode=QUEUE_RUNTIME_CONFIG.get("timeout_mode", "cycle_timeout"),
                camera_correlation_mode=QUEUE_RUNTIME_CONFIG.get("camera_correlation_mode", "sequential"),
                default_payloads=QUEUE_RUNTIME_CONFIG.get("default_payloads", {1: "(1000)E", 2: "(2000)E"}),
                drop_late_camera_data=QUEUE_RUNTIME_CONFIG.get("drop_late_camera_data", True),
                history_limit=HMI_CONFIG.get("history_limit", 1000),
                history_display_limit=HMI_CONFIG.get("history_display_limit", 100),
                prefetch_offset=QUEUE_RUNTIME_CONFIG.get("prefetch_offset", 0),
                prefetch_drop_first=QUEUE_RUNTIME_CONFIG.get("prefetch_drop_first", True),
                completion_count_validation=QUEUE_RUNTIME_CONFIG.get("completion_count_validation", True),
                send_default_on_empty_request=QUEUE_RUNTIME_CONFIG.get("send_default_on_empty_request", False),
            )

            # 3) PLC 双端口处理器：9999(入队) + 9090(出队/反馈)
            self.modules["plc_handler"] = DualPortPLCHandler(
                host=PLC_TCP_CONFIG["host"],
                enqueue_port=PLC_TCP_CONFIG["enqueue_port"],
                dequeue_port=PLC_TCP_CONFIG["dequeue_port"],
            )

            # 4) 相机模块（按流程模式选择）：
            # - legacy_single_camera：花都现场，只有一台 2D 相机，禁用 3D 端口（9002 不监听）
            # - dual_camera_11_12：双相机模式，2D + 3D 同时启动
            if self.enqueue_flow_mode == "legacy_single_camera":
                self.modules["camera_server"] = DualCameraServer(
                    host=CAMERA_SERVER_CONFIG["host"],
                    camera2d_port=CAMERA_SERVER_CONFIG["camera2d_port"],
                    camera3d_port=CAMERA_SERVER_CONFIG["camera3d_port"],
                    camera3d_enabled=False,   # 花都只有 2D 相机，3D 端口不启动
                )
            else:
                self.modules["camera_server"] = DualCameraServer(
                    host=CAMERA_SERVER_CONFIG["host"],
                    camera2d_port=CAMERA_SERVER_CONFIG["camera2d_port"],
                    camera3d_port=CAMERA_SERVER_CONFIG["camera3d_port"],
                    camera3d_enabled=True,
                )

            # 5) 机械臂控制器：接收机械臂命令并下发数据
            self.modules["arm_controller"] = SimpleArmController(
                arms_config=ARM_TCP_CONFIG["arms"],
                connection_mode=ARM_TCP_CONFIG.get("connection_mode", "active"),
                default_listen_host=ARM_TCP_CONFIG.get("listen_host", "0.0.0.0"),
            )

            # 6) HMI 服务：提供网页监控页面与状态 API
            if HMI_CONFIG.get("enabled", True):
                self.modules["dashboard_server"] = DashboardServer(
                    snapshot_provider=self._build_hmi_snapshot,
                    host=HMI_CONFIG["host"],
                    port=HMI_CONFIG["port"],
                )
            return True
        except Exception as exc:
            logger.error("[主程序] 模块初始化失败：%s", exc)
            return False

    def setup_module_callbacks(self):
        """建立模块间回调关系，形成业务链路。"""
        queue_manager: EnhancedQueueManager = self.modules["queue_manager"]
        plc_handler: DualPortPLCHandler = self.modules["plc_handler"]
        arm_controller: SimpleArmController = self.modules["arm_controller"]
        data_parser: EnhancedDataParser = self.modules["data_parser"]

        def plc_feedback_callback(feedback_code: str):
            # 反馈码协议：XN / XD
            # X=机械臂ID，N=已发送未完成，D=已完成。
            # 统一通过 9090 链路发送给 PLC。
            logger.debug("[主程序] 发送 PLC 反馈码=%s", feedback_code)
            plc_handler.send_dequeue_feedback(feedback_code)

        # 这里是两套流程的“总分流点”：
        # - legacy_single_camera：沿用旧单相机回调链
        # - dual_camera_11_12：启用新流程状态机 EnqueueWorkflowCoordinator
        if self.enqueue_flow_mode == "legacy_single_camera":
            # 关键修复：legacy 模式下，相机数据匹配成功后（arm1→回"11"，arm2→回"21"），
            # 需要通过入队通道（9999）发回给 PLC，PLC 才会继续发送下一步命令（11/12）。
            # 原来 plc_stage_cb=None 导致 PLC 永远收不到"11"，
            # 从而不会向软件发 11 命令，相机也就永远收不到第二次触发。
            def plc_stage_callback(stage_code: str):
                logger.debug("[主程序] 发送 PLC 入队阶段反馈=%s", stage_code)
                plc_handler.send_enqueue_feedback(stage_code)

            queue_manager.set_callbacks(
                camera_cb=None,
                plc_feedback_cb=plc_feedback_callback,
                plc_stage_cb=plc_stage_callback,
            )
        else:
            camera_server: DualCameraServer = self.modules["camera_server"]
            workflow = EnqueueWorkflowCoordinator(
                queue_manager=queue_manager,
                plc_handler=plc_handler,
                camera_server=camera_server,
                unknown_enqueue_policy=self.unknown_enqueue_policy,
            )
            self.modules["enqueue_workflow"] = workflow
            queue_manager.set_callbacks(
                camera_cb=None,
                plc_feedback_cb=plc_feedback_callback,
                plc_stage_cb=None,
            )

        def plc_enqueue_callback(plc_data_bytes: bytes):
            # PLC 入队报文 -> 解析 -> 对应流程
            try:
                parsed = data_parser.parse_plc_enqueue_data(plc_data_bytes)
                if self.enqueue_flow_mode == "legacy_single_camera":
                    queue_manager.handle_plc_command(
                        command=parsed.command,
                        sensor_count=parsed.sensor_count,
                        pulse_pointer=parsed.enqueue_pointer,
                    )
                    camera_server: DualCameraServer = self.modules.get("camera_server")
                    if camera_server:
                        normalized = queue_manager._normalize_plc_command(parsed.command)
                        if normalized in (1, 11, 12):
                            trigger_payload = (
                                int(normalized).to_bytes(2, byteorder="big", signed=False)
                                + int(parsed.sensor_count).to_bytes(2, byteorder="big", signed=False)
                            )
                            camera_server.send_to_camera("2d", trigger_payload)
                            logger.info("[主程序] 触发相机 命令=%s 计数=%s", normalized, parsed.sensor_count)
                else:
                    workflow: EnqueueWorkflowCoordinator = self.modules["enqueue_workflow"]
                    # 新流程入口：只把 PLC 入队帧解包后交给 workflow，
                    # 具体 11/12 时序、2D/3D 联动与 Done 回PLC 都在 workflow 内处理。
                    workflow.handle_plc_enqueue(
                        command=parsed.command,
                        sensor_count=parsed.sensor_count,
                        pulse_pointer=parsed.enqueue_pointer,
                    )
            except Exception as exc:
                logger.error("[主程序] 入队报文解析失败：%s", exc)

        def plc_dequeue_callback(plc_data_bytes: bytes):
            # PLC 出队报文 -> 解析指针 -> 仅变化时触发缓存更新
            try:
                queue_manager.handle_dequeue_pointers(plc_data_bytes)
            except Exception as exc:
                logger.error("[主程序] 出队报文解析失败：%s", exc)

        plc_handler.set_callbacks(enqueue_cb=plc_enqueue_callback, dequeue_cb=plc_dequeue_callback)

        if self.enqueue_flow_mode == "legacy_single_camera":
            camera_server: DualCameraServer = self.modules["camera_server"]

            # 注意：DualCameraServer._recv_loop 调用回调的签名为 callback(camera_key, data)，
            # 所以这里第一个参数必须是 camera_key: str，第二个才是 raw_data: bytes。
            # 之前参数顺序颠倒导致 raw_data 实际接收到字符串"2d"，decode() 必然报错。
            def camera_data_callback(camera_key: str, raw_data: bytes):
                logger.debug("[主程序] 单相机数据 通道=%s 字节数=%s", camera_key, len(raw_data))
                try:
                    extract_mode = QUEUE_RUNTIME_CONFIG.get("camera_count_extract_mode", "auto")
                    camera_count = data_parser.extract_camera_count_from_payload(
                        raw_data,
                        mode=str(extract_mode),
                    )
                    queue_manager.ingest_camera_data(raw_data, camera_count=camera_count)
                except Exception as exc:
                    logger.error("[主程序] 单相机数据处理失败：%s", exc)

            camera_server.set_data_callback(camera_data_callback)
        else:
            camera_server: DualCameraServer = self.modules["camera_server"]
            workflow: EnqueueWorkflowCoordinator = self.modules["enqueue_workflow"]
            camera_server.set_data_callback(workflow.on_camera_payload)

        def arm_command_callback(arm_id: int, command: str):
            # 机械臂协议：
            # data -> 请求缓存数据（队列模块反馈 XN 给 PLC）
            # 1 / 1,count -> 收到数据后的完成确认（队列模块反馈 XD 给 PLC）
            raw = (command or "").strip()
            cmd = raw.lower()
            if cmd == "data":
                payload = queue_manager.handle_arm_data_request(arm_id)
                if payload is not None:
                    send_ok = arm_controller.send_data(arm_id, payload)
                    if send_ok:
                        logger.debug(
                            "[主程序] 机械臂=%s 数据发送成功 字节数=%s",
                            arm_id,
                            len(payload),
                        )
                    else:
                        logger.warning(
                            "[主程序] 机械臂=%s 数据发送失败 字节数=%s",
                            arm_id,
                            len(payload),
                        )
            else:
                # 支持：
                # - "1"
                # - "1,123"
                # - "1 123"
                # - "1:123"
                match = re.match(r"^1(?:\s*[,:\s]\s*(\d+))?$", cmd)
                if match:
                    reported_count = int(match.group(1)) if match.group(1) is not None else None
                    queue_manager.handle_arm_completion(arm_id, reported_count=reported_count)

        arm_controller.set_command_callback(arm_command_callback)

    def connect_modules(self) -> bool:
        """启动网络端口、连接设备并启动 HMI。"""
        try:
            # PLC 双端口属于主链路入口，启动失败则直接失败。
            if not self.modules["plc_handler"].start_servers():
                return False

            if self.enqueue_flow_mode == "legacy_single_camera":
                if not self.modules["camera_server"].start():
                    logger.warning("[主程序] 单相机服务启动失败，继续启动其他模块")
            else:
                # 新流程：启动2D/3D相机监听端口
                if not self.modules["camera_server"].start():
                    return False

            # 机械臂连接结果仅用于监控与诊断。
            arm_result = self.modules["arm_controller"].connect_all()
            logger.info("[主程序] 机械臂连接结果：%s", arm_result)

            # HMI 失败不影响主流程。
            dashboard = self.modules.get("dashboard_server")
            if dashboard:
                if dashboard.start():
                    logger.info("[主程序] HMI 页面已启动：http://%s:%s", HMI_CONFIG["host"], HMI_CONFIG["port"])
                else:
                    logger.warning("[主程序] HMI 页面启动失败")
            return True
        except Exception as exc:
            logger.error("[主程序] 模块连接启动失败：%s", exc)
            return False

    def _build_hmi_snapshot(self) -> Dict[str, Any]:
        """提供给 HMI 的统一状态快照。"""
        queue_manager = self.modules.get("queue_manager")
        if not queue_manager:
            return {"error": "queue manager unavailable"}

        # 队列快照是主数据，再补充其他模块连接态。
        snapshot = queue_manager.get_hmi_snapshot()

        plc_handler = self.modules.get("plc_handler")
        if plc_handler:
            snapshot["plc_status"] = plc_handler.get_status()

        if self.enqueue_flow_mode == "legacy_single_camera":
            camera_server = self.modules.get("camera_server")
            if camera_server:
                channels = camera_server.get_status()
                snapshot["camera"] = {
                    "connected": any(item.get("connected") for item in channels.values()),
                    "channels": channels,
                    "mode": "legacy_single_camera",
                }
        else:
            camera_server = self.modules.get("camera_server")
            if camera_server:
                channels = camera_server.get_status()
                snapshot["camera"] = {
                    "connected": any(item.get("connected") for item in channels.values()),
                    "channels": channels,
                    "mode": "dual_camera_11_12",
                }

        enqueue_flow: Dict[str, Any] = {
            "mode": self.enqueue_flow_mode,
            "unknown_enqueue_policy": self.unknown_enqueue_policy,
        }
        workflow = self.modules.get("enqueue_workflow")
        if workflow and hasattr(workflow, "get_status"):
            try:
                enqueue_flow["runtime"] = workflow.get_status()
            except Exception as exc:
                enqueue_flow["runtime_error"] = str(exc)
        snapshot["enqueue_flow"] = enqueue_flow

        arm_controller = self.modules.get("arm_controller")
        if arm_controller:
            snapshot["arm_connections"] = arm_controller.get_all_status()

        return snapshot

    def start_system(self):
        """系统启动主流程。"""
        try:
            # 启动顺序：初始化 -> 回调连线 -> 连接模块 -> 主循环监控
            if not self.initialize_modules():
                return False
            self.setup_module_callbacks()
            if not self.connect_modules():
                return False

            self.system_running = True
            while self.system_running:
                self._system_monitor()
                time.sleep(1.0)
        except KeyboardInterrupt:
            logger.info("[主程序] 收到键盘中断")
        except Exception as exc:
            logger.error("[主程序] 运行异常：%s", exc)
        finally:
            self.shutdown()

    def _system_monitor(self):
        """周期性监控日志。"""
        try:
            # 仅采样状态，不做业务修改。
            queue_manager: EnhancedQueueManager = self.modules["queue_manager"]
            queue_status = queue_manager.get_system_statistics()
            plc_status = self.modules["plc_handler"].get_status()
            if self.enqueue_flow_mode == "legacy_single_camera":
                camera_channels = self.modules["camera_server"].get_status()
                camera_connected = any(item.get("connected") for item in camera_channels.values())
            else:
                camera_channels = self.modules["camera_server"].get_status()
                camera_connected = any(item.get("connected") for item in camera_channels.values())
            arm_status = self.modules["arm_controller"].get_all_status()

            if time.time() % 10 < 1:
                active_arms = len([v for v in arm_status.values() if v == "CONNECTED"])
                logger.info(
                    "[监控] 队列数量=%s PLC连接数=%s 相机在线=%s 在线机械臂=%s",
                    queue_status["queue_info"]["current_size"],
                    plc_status["active_connections"],
                    camera_connected,
                    active_arms,
                )
        except Exception as exc:
            logger.warning("[监控] 状态采样失败：%s", exc)

    def shutdown(self):
        """优雅关闭：先停主循环，再逐个 stop 模块。"""
        if not self.system_running and not self.modules:
            return

        self.system_running = False

        try:
            if "plc_handler" in self.modules:
                self.modules["plc_handler"].stop()
            if "camera_server" in self.modules:
                self.modules["camera_server"].stop()
            if "arm_controller" in self.modules:
                self.modules["arm_controller"].stop()
            if "dashboard_server" in self.modules:
                self.modules["dashboard_server"].stop()
        except Exception as exc:
            logger.error("[主程序] 关闭过程异常：%s", exc)


def main():
    """控制台入口。"""
    print("=== 工业自动化队列管理系统 v2.0 ===")
    print(f"运行平台：{SYSTEM_CONFIG['platform']}")
    print(f"Python版本：{SYSTEM_CONFIG['python_version']}")
    print(
        f"PLC端口：入队={PLC_TCP_CONFIG['enqueue_port']} "
        f"出队={PLC_TCP_CONFIG['dequeue_port']}"
    )
    flow_mode = str(QUEUE_RUNTIME_CONFIG.get("enqueue_flow_mode", "dual_camera_11_12")).lower()
    if flow_mode == "legacy_single_camera":
        print(f"相机模式：单相机 -> {CAMERA_TCP_CONFIG['host']}:{CAMERA_TCP_CONFIG['port']}")
    else:
        print(
            "相机模式：2D+3D双相机 -> "
            f"2D:{CAMERA_SERVER_CONFIG['camera2d_port']} 3D:{CAMERA_SERVER_CONFIG['camera3d_port']}"
        )
    if HMI_CONFIG.get("enabled", True):
        print(f"HMI页面：http://{HMI_CONFIG['host']}:{HMI_CONFIG['port']}")

    system = EnhancedIndustrialSystem()
    try:
        system.start_system()
    except Exception as exc:
        logger.error("系统启动失败：%s", exc)
        sys.exit(1)


if __name__ == "__main__":
    # 启动前确保能捕获所有异常并写入日志文件
    try:
        main()
    except Exception as e:
        import traceback
        # 尝试写入错误日志
        try:
            if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
                APP_ROOT = Path(sys._MEIPASS)
            else:
                APP_ROOT = Path(__file__).resolve().parents[1]
            error_log = os.path.join(APP_ROOT, "logs", "error.log")
            os.makedirs(os.path.dirname(error_log), exist_ok=True)
            with open(error_log, "a", encoding="utf-8") as f:
                f.write(traceback.format_exc())
        except:
            pass
        print(f"严重错误：{e}")
        print(traceback.format_exc())
        input("按回车退出...")
        sys.exit(1)
