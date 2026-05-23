"""
系统配置文件。

阅读建议：
1. 先看 PLC_TCP_CONFIG（决定 9999/9090 网络链路）。
2. 再看 ARM_TCP_CONFIG / CAMERA_SERVER_CONFIG（决定设备连接参数）。
3. 最后看 QUEUE_CONFIG / HMI_CONFIG（决定运行策略和可视化）。
"""

import os
from typing import Dict, Any

# ==================== 系统基本信息 ====================
# 仅用于展示和日志标识，不参与业务判断。
SYSTEM_CONFIG: Dict[str, Any] = {
    'name': '工业自动化队列管理系统',
    'version': '2.0.0',
    'platform': 'Windows',
    'python_version': '3.8+',
    'encoding': 'utf-8'
}

# ==================== 双PLC连接配置 ====================
# 关键约定：
# - enqueue_port：PLC -> 软件 的入队数据通道
# - dequeue_port：PLC -> 软件 的出队指针通道，同时也是 软件 -> PLC 的反馈通道
PLC_TCP_CONFIG: Dict[str, Any] = {
    'host': '0.0.0.0',        # 本机监听地址（0.0.0.0表示监听所有网络接口）
    'enqueue_port': 9999,           # 入队连接端口
    'dequeue_port': 9090,           # 出队连接端口
    # 兼容旧流程的阶段握手通道配置（新流程默认不使用11/21握手）：
    # - enqueue：走9999
    # - dequeue：走9090
    # - both：双通道都发送
    # 当前流程属于入队握手，默认使用 enqueue(9999)。
    'stage_feedback_channel': 'enqueue',
    'keep_alive': True,             # TCP长连接保持
    'keep_idle': 30,                # 空闲时间(秒)
    'keep_interval': 5,             # 心跳间隔(秒)
    'timeout': 5.0,                 # 连接超时时间
    'max_connections': 10,          # 最大连接数
    'reconnect_delay': 2.0          # 重连延迟时间
}

# ==================== 相机服务端配置 ====================
# 软件作为服务端，2D/3D 相机作为客户端连接进来。
CAMERA_SERVER_CONFIG: Dict[str, Any] = {
    'host': '0.0.0.0',        # 本机监听地址（0.0.0.0表示监听所有网络接口）
    'camera2d_port': 9001,          # 2D 相机接入端口
    'camera3d_port': 9002,          # 3D 相机接入端口
    'camera2d_ip': '192.168.2.77',  # 2D 相机现场IP（用于日志）
    'camera3d_ip': '192.168.2.88',  # 3D 相机现场IP（用于日志）
}

# ==================== 旧单相机客户端配置（兼容保留） ====================
# legacy_single 模式下使用：软件作为客户端主动连接相机。
CAMERA_TCP_CONFIG: Dict[str, Any] = {
    'host': '192.168.2.88',
    'port': 8080,
    'timeout': 3.0,
    'capture_timeout': 2.0,
    'retry_times': 3,
    'buffer_size': 1024
}

# ==================== 机械臂连接配置 ====================
# connection_mode:
# - active：软件主动连接机械臂（旧模式）
# - passive：软件监听端口，机械臂主动连接（现场推荐）
#
# listen_host：被动模式下软件监听地址（通常 0.0.0.0）
#
# arms 字段说明：
# - host：机械臂设备IP（用于日志/来源校验；被动模式不用于 bind）
# - port：该机械臂连接到软件时使用的目标端口
ARM_TCP_CONFIG: Dict[str, Any] = {
    'connection_mode': 'passive',
    'listen_host': '0.0.0.0',
    'arms': {
        1: {'host': '192.168.2.31', 'port': 4000, 'name': '机械臂1'},
        2: {'host': '192.168.2.32', 'port': 4001, 'name': '机械臂2'}
    },
    'timeout': 3.0,                 # 连接超时时间
    'command_timeout': 1.0,         # 命令超时时间
    'max_retry': 3                  # 最大重试次数
}

# ==================== 队列配置 ====================
# 注意：
# - 目前核心实现的溢出策略固定为“覆盖最老项”
# - overflow_action 字段在当前版本保留用于未来扩展
QUEUE_CONFIG: Dict[str, Any] = {
    'length': 10,                   # 队列最大长度
    'max_count': 65535,             # 传感器计数最大值
    'pointer_method': 'modulo',     # 指针计算方法
    'overflow_action': 'reset',     # 溢出处理方式
    'double_queue_enabled': True,   # 双队列轮转支持
    'queue_switch_threshold': 5,    # 队列切换阈值
    'process_timeout': 30.0         # 处理超时时间(秒)
}

# ==================== 队列运行策略 ====================
# timeout_mode:
# - cycle_timeout：下一次01到来时，上一轮未收到相机数据则填默认值
# - dequeue_timeout：出队时仍无相机数据则填默认值后正常出队
# camera_correlation_mode:
# - sequential：按命令顺序匹配相机返回（非并发场景）
# - counted：按相机回包中的计数匹配（并发场景）
QUEUE_RUNTIME_CONFIG: Dict[str, Any] = {
    # 超时模式：
    # - cycle_timeout：下一次01命令到来时，上一轮未收到相机数据则填默认值
    #   （同时发送PLC回复，让PLC继续流程，不会卡在当前工件）
    # - dequeue_timeout：机械臂请求数据时仍无相机数据则填默认值
    'timeout_mode': 'cycle_timeout',
    # 入队流程模式：
    # - dual_camera_11_12：新流程（11/12 + 2D/3D）
    # - legacy_single_camera：旧流程（单相机客户端）
    'enqueue_flow_mode': 'legacy_single_camera',
    # 新流程下非11/12入队命令处理：
    # - ignore：忽略（默认，避免误触发旧流程）
    # - legacy：回退到旧 queue_manager 处理
    'unknown_enqueue_policy': 'ignore',
    
    # 相机数据与槽位匹配模式（重要：切换相机数据格式时修改此参数）：
    # - sequential：按顺序匹配（默认）。适用于相机数据不带 count、按顺序到达的场景
    #   → 相机返回格式：(1001,点位数据)E，队列管理器会自动插入 count 变成 (1001,5,点位数据)E
    # - counted：按计数匹配。适用于相机数据带 count、可能乱序到达的场景
    #   → 相机返回格式：(1001,5,点位数据)E，队列管理器根据 count 进行匹配
    #   注意：counted 模式需要 camera_count_extract_mode 能够正确解析出 count
    'camera_correlation_mode': 'sequential',

    # 相机计数提取策略（counted 模式会使用）：
    # - auto：先按 ASCII COUNT/CNT 提取；若未命中且报文明显非文本，则按二进制提取
    # - ascii：仅按 ASCII COUNT/CNT 提取
    # - binary：仅按二进制头部(flag(2)+count(2))提取
    # - disabled：不提取计数（始终返回None），此时会使用 slot 中的 count
    'camera_count_extract_mode': 'auto',
    
    'prefetch_offset': 1,          # 预取偏移：1表示当前拍下发下一工件数据
    'prefetch_drop_first': True,   # 首件暖机：预取不足时允许首件空转
    'completion_count_validation': True,  # 机械臂回包1,count时启用计数校验
    # 机械臂发送 data 但当前无缓存时，是否回默认值而非静默不发。
    'send_default_on_empty_request': True,
    'default_payloads': {
        1: '(1000)E',              # 机械臂1默认值（临时）
        2: '(2000)E'               # 机械臂2默认值（临时）
    },
    'drop_late_camera_data': True  # 超时或出队后到达的数据直接丢弃
}

# ==================== 协议扩展配置 ====================
# 用于后期通讯协议变更时的扩展支持，通过配置切换无需修改代码
# 当前版本默认使用 v1 协议
PROTOCOL_CONFIG: Dict[str, Any] = {
    # 协议版本：用于区分不同的通讯协议格式
    # - v1：当前默认版本
    # - v2：后续扩展版本
    'version': 'v1',
    
    # PLC 命令格式：
    # - hex：十六进制（如 0x01, 0x11, 0x12）
    # - ascii：ASCII文本（如 "01", "11", "12"）
    'plc_command_format': 'hex',
    
    # 相机计数提取位置：
    # - binary：二进制头部 (flag(2字节) + count(2字节))
    # - ascii：ASCII文本中的 COUNT 字段
    # - auto：自动检测
    'camera_count_position': 'auto',
    
    # 机械臂反馈码格式：
    # - xn_xd：传统格式（如 1N, 1D, 2N, 2D）
    # - json：JSON格式（扩展用）
    'arm_feedback_format': 'xn_xd',
    
    # 入队阶段握手码格式：
    # - legacy：传统格式（如 11, 21, 1Done, 2Done）
    # - modern：扩展格式（预留）
    'enqueue_stage_format': 'legacy',
}

# 协议版本映射：不同版本使用不同的配置项（预留扩展）
# 后续新增协议版本时，可在此处添加版本号到配置文件的映射关系

# ==================== 出队指针配置 ====================
# 控制出队指针相关行为（校验、缓存保留、重复请求支持）。
DEQUEUE_POINTER_CONFIG: Dict[str, Any] = {
    'pointer_size': 2,              # 每个指针占用字节数
    'max_arms': 8,                  # 最大支持机械臂数量
    'default_pointer': 1,           # 默认指针值
    'pointer_validation': True,     # 指针验证开关
    'cache_retention': True,        # 缓存保留机制
    'repeat_request_support': True  # 重复请求支持
}

# ==================== 数据格式配置 ====================
# 声明各类报文的格式约定，便于解析器和设备侧对齐。
DATA_FORMAT: Dict[str, Any] = {
    # PLC入队数据格式
    'plc_enqueue_format': {
        'command_bytes': 2,         # 命令字节数
        'sensor_count_bytes': 2,    # 传感器计数字节数
        'pointer_bytes': 2,         # 指针字节数
        # 入队字段顺序（新流程为：命令 + 计数 + 指针）
        # 可选：
        # - command_count_pointer
        # - command_pointer_count
        'field_order': 'command_count_pointer',
        'byte_order': 'big'         # 字节序: big/little
    },
    
    # PLC出队数据格式
    'plc_dequeue_format': {
        'total_bytes': 4,           # 总字节数(4字节=2个指针)
        'pointer_bytes': 2,         # 每个指针字节数
        'byte_order': 'big'         # 字节序
    },
    
    # 相机数据格式
    'camera_format': {
        'max_length': 255,          # 最大长度(字节)
        'float_precision': 32,      # 浮点数精度(位)
        'compression_enabled': True, # 压缩开关
        'end_marker': b'E'          # 结束标记
    },
    
    # 机械臂数据格式
    'arm_format': {
        'flag_bytes': 2,            # 标志位字节数
        'count_bytes': 2,           # 计数字节数
        'pointer_bytes': 2,         # 指针字节数
        'point_bytes': 4,           # 点位信息字节数(32位float)
        'end_marker': b'E'          # 结束标记
    }
}

# ==================== 日志配置 ====================
# 控制日志输出级别、格式和落盘路径。
# log_mode 说明：
#   - simple: 单一日志文件(system.log)，不记录原始通讯内容，时间戳到秒
#   - detailed: 多个日志文件(enqueue.log/dequeue.log/system.log)，记录原始通讯内容，时间戳到毫秒
LOGGING_CONFIG: Dict[str, Any] = {
    'log_level': 'DEBUG',            # 日志级别
    'log_format': '[%(asctime)s] [%(levelname_cn)s] [%(name)s] %(message)s',
    'date_format': '%H:%M:%S',      # 时间格式(精确到秒)
    'timestamp_ms': False,          # 是否显示毫秒 True=显示 / False=不显示
    'log_mode': 'detailed',           # 日志模式 simple=简单模式 / detailed=详细模式
    'console_enabled': True,         # 是否输出到终端窗口
    'console_level': 'WARNING',      # 终端只显示低频诊断，避免现场通讯被终端刷新阻塞
    'raw_log_to_console': False,     # 原始通讯日志只写文件，不写终端
    'log_file': 'logs/system.log',  # 日志文件路径(仅 simple 模式使用)
    'enqueue_log_file': 'logs/enqueue.log',  # 入队日志文件路径
    'dequeue_log_file': 'logs/dequeue.log',  # 出队日志文件路径

    # 日志文件大小限制（按现场 6000次/天 计算，详细模式每天约 12MB）：
    # - 500MB 单文件约可记录 40-50 天的数据
    # - 配合 backup_count=5 轮转，总共可保留约 200-250 天
    'max_file_size': 500 * 1024 * 1024,  # 最大文件大小(500MB)
    'backup_count': 5               # 备份数量
}

# ==================== HMI可视化配置 ====================
# 控制前端可视化页面服务。
HMI_CONFIG: Dict[str, Any] = {
    'enabled': True,                # 启用可视化页面
    'host': '127.0.0.1',            # HMI服务监听地址
    'port': 8090,                   # HMI服务端口
    'refresh_ms': 1000,             # 前端刷新间隔
    'history_limit': 1000,          # 后端历史记录保留条数
    'history_display_limit': 100    # 页面每次最多显示条数
}

# ==================== 环境变量配置 ====================
# 覆盖规则：
# - 变量名格式：QUEUE_<KEY>
# - 例如：QUEUE_ENQUEUE_PORT=10001、QUEUE_ENABLED=true
def get_config_from_env(config_dict: Dict[str, Any]) -> Dict[str, Any]:
    """从环境变量获取配置"""
    for key, value in config_dict.items():
        env_key = f"QUEUE_{key.upper()}"
        if env_key in os.environ:
            # 根据值类型进行转换
            if isinstance(value, bool):
                config_dict[key] = os.environ[env_key].lower() in ('true', '1', 'yes')
            elif isinstance(value, int):
                config_dict[key] = int(os.environ[env_key])
            elif isinstance(value, float):
                config_dict[key] = float(os.environ[env_key])
            else:
                config_dict[key] = os.environ[env_key]
    return config_dict

# 应用环境变量配置（启动时执行一次）
PLC_TCP_CONFIG = get_config_from_env(PLC_TCP_CONFIG)
CAMERA_SERVER_CONFIG = get_config_from_env(CAMERA_SERVER_CONFIG)
CAMERA_TCP_CONFIG = get_config_from_env(CAMERA_TCP_CONFIG)
QUEUE_CONFIG = get_config_from_env(QUEUE_CONFIG)
HMI_CONFIG = get_config_from_env(HMI_CONFIG)

# ==================== 配置验证 ====================
# 在模块导入阶段进行基础校验，尽早暴露明显配置问题。
def validate_config() -> bool:
    """验证配置的有效性"""
    # 验证端口范围
    if not (1 <= PLC_TCP_CONFIG['enqueue_port'] <= 65535):
        raise ValueError("入队端口号超出有效范围")
    if not (1 <= PLC_TCP_CONFIG['dequeue_port'] <= 65535):
        raise ValueError("出队端口号超出有效范围")

    if not (1 <= CAMERA_SERVER_CONFIG['camera2d_port'] <= 65535):
        raise ValueError("2D相机端口号超出有效范围")
    if not (1 <= CAMERA_SERVER_CONFIG['camera3d_port'] <= 65535):
        raise ValueError("3D相机端口号超出有效范围")
    if not (1 <= CAMERA_TCP_CONFIG['port'] <= 65535):
        raise ValueError("单相机端口号超出有效范围")
    
    # 验证端口不重复
    if PLC_TCP_CONFIG['enqueue_port'] == PLC_TCP_CONFIG['dequeue_port']:
        raise ValueError("入队和出队端口号不能相同")
    if CAMERA_SERVER_CONFIG['camera2d_port'] == CAMERA_SERVER_CONFIG['camera3d_port']:
        raise ValueError("2D和3D相机端口号不能相同")

    flow_mode = str(QUEUE_RUNTIME_CONFIG.get('enqueue_flow_mode', 'dual_camera_11_12')).lower()
    if flow_mode not in ('dual_camera_11_12', 'legacy_single_camera'):
        raise ValueError("enqueue_flow_mode 必须是 dual_camera_11_12/legacy_single_camera 之一")
    unknown_policy = str(QUEUE_RUNTIME_CONFIG.get('unknown_enqueue_policy', 'ignore')).lower()
    if unknown_policy not in ('ignore', 'legacy'):
        raise ValueError("unknown_enqueue_policy 必须是 ignore/legacy 之一")
    # 验证阶段握手反馈通道配置
    if PLC_TCP_CONFIG.get('stage_feedback_channel') not in ('enqueue', 'dequeue', 'both'):
        raise ValueError("stage_feedback_channel 必须是 enqueue/dequeue/both 之一")
    
    # 验证队列长度
    if QUEUE_CONFIG['length'] <= 0:
        raise ValueError("队列长度必须大于0")
    
    return True

# 启动时验证配置
try:
    validate_config()
    print("[配置] 配置验证通过")
except ValueError as e:
    print(f"[配置] 配置验证失败：{e}")
