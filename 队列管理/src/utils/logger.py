"""
日志工具模块 (logger.py)

提供标准化的日志记录功能，支持：
1. 分文件记录：enqueue.log（入队）、dequeue.log（出队）、system.log（系统）
2. 时间戳精度：可配置是否显示毫秒
3. 日志模式：simple（简单模式）或 detailed（详细模式）

详细模式配置说明：
    simple:   单一日志文件，不记录原始通讯内容，时间戳到秒
    detailed: 多个日志文件，记录原始通讯内容，时间戳到毫秒

使用示例：
    from utils.logger import get_logger, get_enqueue_logger, get_dequeue_logger
    
    logger = get_logger("MODULE")           # 获取通用日志器
    enqueue_logger = get_enqueue_logger()  # 获取入队专用日志器
    dequeue_logger = get_dequeue_logger()  # 获取出队专用日志器
"""

import logging
import os
import sys
from datetime import datetime
from pathlib import Path
from typing import Optional, Dict, Any

# 全局配置缓存
_logging_config: Dict[str, Any] = {}

_LEVEL_NAME_CN = {
    "DEBUG": "调试",
    "INFO": "信息",
    "WARNING": "警告",
    "ERROR": "错误",
    "CRITICAL": "严重",
}


def _load_logging_config():
    """
    加载日志配置
    
    优先从 config.settings 读取配置，如果失败则使用默认配置
    这样可以避免循环导入问题
    """
    global _logging_config
    if _logging_config:
        return _logging_config
    
    try:
        # 尝试导入配置
        sys.path.insert(0, str(Path(__file__).parent.parent / "config"))
        from config.settings import LOGGING_CONFIG
        _logging_config = LOGGING_CONFIG
    except (ImportError, AttributeError):
        # 配置加载失败时使用默认配置
        _logging_config = {
            'log_level': 'INFO',
            'log_format': '[%(asctime)s] [%(levelname_cn)s] [%(name)s] %(message)s',
            'timestamp_ms': False,
            'log_mode': 'simple',
            'console_enabled': True,
            'console_level': 'WARNING',
            'raw_log_to_console': False,
            'log_file': 'logs/system.log',
            'enqueue_log_file': 'logs/enqueue.log',
            'dequeue_log_file': 'logs/dequeue.log',
        }
    
    return _logging_config


def _get_timestamp() -> str:
    """
    获取带毫秒的时间戳
    
    Returns:
        str: 时间字符串，格式为 HH:MM:SS.fff（如果开启毫秒）或 HH:MM:SS
    """
    config = _load_logging_config()
    now = datetime.now()
    
    if config.get('timestamp_ms', False):
        # 显示毫秒，格式：16:33:52.123
        return now.strftime('%H:%M:%S.') + f'{now.microsecond // 1000:03d}'
    else:
        # 不显示毫秒，格式：16:33:52
        return now.strftime('%H:%M:%S')


class _MsFormatter(logging.Formatter):
    """
    自定义日志格式化器，支持毫秒级时间戳
    
    通过读取配置动态决定是否显示毫秒
    """
    
    def format(self, record):
        record.levelname_cn = _LEVEL_NAME_CN.get(record.levelname, record.levelname)
        return super().format(record)

    def formatTime(self, record, datefmt=None):
        """
        重写 formatTime 方法，支持动态时间格式
        
        Args:
            record: 日志记录对象
            datefmt: 时间格式字符串
            
        Returns:
            str: 格式化后的时间字符串
        """
        config = _load_logging_config()
        ct = datetime.fromtimestamp(record.created)
        
        if config.get('timestamp_ms', False):
            # 显示毫秒
            return ct.strftime('%H:%M:%S.') + f'{ct.microsecond // 1000:03d}'
        else:
            # 不显示毫秒
            if datefmt:
                return ct.strftime(datefmt)
            return ct.strftime('%H:%M:%S')


def _get_level(level_name: str, default: int = logging.INFO) -> int:
    if isinstance(level_name, int):
        return level_name
    return getattr(logging, str(level_name or "").upper(), default)


def _create_formatter(config: Dict[str, Any]) -> _MsFormatter:
    datefmt = config.get('date_format') if not config.get('timestamp_ms', False) else None
    return _MsFormatter(
        fmt=config.get('log_format', '[%(asctime)s] [%(levelname_cn)s] [%(name)s] %(message)s'),
        datefmt=datefmt,
    )


def _create_console_handler(config: Dict[str, Any], formatter: logging.Formatter) -> Optional[logging.Handler]:
    if not config.get('console_enabled', True):
        return None
    console_handler = logging.StreamHandler()
    console_handler.setLevel(_get_level(config.get('console_level', 'WARNING'), logging.WARNING))
    console_handler.setFormatter(formatter)
    return console_handler


def _setup_simple_mode():
    """
    配置简单日志模式（单一日志文件）
    
    所有日志输出到同一个文件（system.log），不记录原始通讯内容
    """
    config = _load_logging_config()
    
    # 获取日志目录和文件路径
    log_file = config.get('log_file', 'logs/system.log')
    log_dir = os.path.dirname(log_file)
    
    # 创建日志目录
    if log_dir and not os.path.exists(log_dir):
        os.makedirs(log_dir)
    
    # 生成带日期的日志文件名
    timestamp = datetime.now().strftime("%Y%m%d")
    base_name = os.path.basename(log_file)
    name_without_ext = os.path.splitext(base_name)[0]
    ext = os.path.splitext(base_name)[1]
    log_file_with_date = os.path.join(log_dir, f"{name_without_ext}_{timestamp}{ext}")
    
    formatter = _create_formatter(config)
    
    # 文件处理器
    file_handler = logging.FileHandler(log_file_with_date, encoding='utf-8')
    file_handler.setLevel(_get_level(config.get('log_level', 'DEBUG'), logging.DEBUG))
    file_handler.setFormatter(formatter)
    
    # 配置根日志器
    root_logger = logging.getLogger()
    root_logger.setLevel(_get_level(config.get('log_level', 'DEBUG'), logging.DEBUG))
    root_logger.handlers.clear()
    root_logger.addHandler(file_handler)
    console_handler = _create_console_handler(config, formatter)
    if console_handler:
        root_logger.addHandler(console_handler)


def _setup_detailed_mode():
    """
    配置详细日志模式（分文件记录）
    
    日志分类：
        - enqueue.log: 入队流程（PLC入队请求、相机数据）
        - dequeue.log: 出队流程（PLC出队请求、机械臂通讯）
        - system.log: 系统状态、异常、业务状态变化等
    """
    config = _load_logging_config()
    
    # 获取各日志文件路径
    log_dir = 'logs'
    enqueue_log = config.get('enqueue_log_file', 'logs/enqueue.log')
    dequeue_log = config.get('dequeue_log_file', 'logs/dequeue.log')
    system_log = config.get('system_file', 'logs/system.log')
    
    # 创建日志目录
    if not os.path.exists(log_dir):
        os.makedirs(log_dir)
    
    # 生成带日期的日志文件名
    timestamp = datetime.now().strftime("%Y%m%d")
    
    def get_dated_log_file(base_path: str) -> str:
        """生成带日期的日志文件名"""
        dir_name = os.path.dirname(base_path)
        base_name = os.path.basename(base_path)
        name_without_ext = os.path.splitext(base_name)[0]
        ext = os.path.splitext(base_name)[1]
        return os.path.join(dir_name, f"{name_without_ext}_{timestamp}{ext}")
    
    enqueue_file = get_dated_log_file(enqueue_log)
    dequeue_file = get_dated_log_file(dequeue_log)
    system_file = get_dated_log_file(system_log)
    
    formatter = _create_formatter(config)
    
    # 创建入队日志处理器
    enqueue_handler = logging.FileHandler(enqueue_file, encoding='utf-8')
    enqueue_handler.setLevel(logging.DEBUG)
    enqueue_handler.setFormatter(formatter)
    
    # 创建出队日志处理器
    dequeue_handler = logging.FileHandler(dequeue_file, encoding='utf-8')
    dequeue_handler.setLevel(logging.DEBUG)
    dequeue_handler.setFormatter(formatter)
    
    # 创建系统日志处理器
    system_handler = logging.FileHandler(system_file, encoding='utf-8')
    system_handler.setLevel(_get_level(config.get('log_level', 'DEBUG'), logging.DEBUG))
    system_handler.setFormatter(formatter)
    
    # 配置根日志器
    root_logger = logging.getLogger()
    root_logger.setLevel(_get_level(config.get('log_level', 'DEBUG'), logging.DEBUG))
    root_logger.handlers.clear()

    root_logger.addHandler(system_handler)
    console_handler = _create_console_handler(config, formatter)
    if console_handler:
        root_logger.addHandler(console_handler)
    
    # 创建专用的入队日志器
    _enqueue_logger = logging.getLogger('ENQUEUE')
    _enqueue_logger.setLevel(logging.DEBUG)
    _enqueue_logger.handlers.clear()
    _enqueue_logger.addHandler(enqueue_handler)
    if config.get('raw_log_to_console', False) and console_handler:
        _enqueue_logger.addHandler(console_handler)
    _enqueue_logger.propagate = False
    
    # 创建专用的出队日志器
    _dequeue_logger = logging.getLogger('DEQUEUE')
    _dequeue_logger.setLevel(logging.DEBUG)
    _dequeue_logger.handlers.clear()
    _dequeue_logger.addHandler(dequeue_handler)
    if config.get('raw_log_to_console', False) and console_handler:
        _dequeue_logger.addHandler(console_handler)
    _dequeue_logger.propagate = False


class SystemLogger:
    """
    系统日志管理器
    
    根据配置自动选择简单模式或详细模式：
        - simple:   单一日志文件
        - detailed: 分类日志文件
    """
    
    def __init__(self, log_dir: str = "logs"):
        """
        初始化日志系统
        
        Args:
            log_dir: 日志文件存储目录（已被配置中的路径替代，此参数保留用于兼容）
        """
        self._setup_logging()
    
    def _setup_logging(self):
        """
        根据配置初始化日志系统
        
        根据 LOGGING_CONFIG 中的 log_mode 配置决定使用哪种日志模式：
            - simple:   _setup_simple_mode()
            - detailed: _setup_detailed_mode()
        """
        config = _load_logging_config()
        log_mode = config.get('log_mode', 'simple')
        
        if log_mode == 'detailed':
            _setup_detailed_mode()
        else:
            _setup_simple_mode()
        
        # 记录启动信息
        logger = logging.getLogger()
        logger.info(
            "[日志] 模式=%s 毫秒时间戳=%s 终端输出=%s 终端级别=%s 原始通讯写终端=%s",
            log_mode,
            config.get('timestamp_ms', False),
            config.get('console_enabled', True),
            config.get('console_level', 'WARNING'),
            config.get('raw_log_to_console', False),
        )
    
    @staticmethod
    def get_logger(name: str) -> logging.Logger:
        """
        获取指定名称的日志器
        
        Args:
            name: 日志器名称
            
        Returns:
            logging.Logger: 配置好的日志器
        """
        return logging.getLogger(name)
    
    @staticmethod
    def log_system_event(event_type: str, message: str, 
                        module: str = "SYSTEM", **kwargs):
        """
        记录系统事件日志
        
        Args:
            event_type: 事件类型 (START/STOP/ERROR/WARNING/INFO)
            message: 事件消息
            module: 模块名称
            **kwargs: 额外参数
        """
        logger = SystemLogger.get_logger(module)
        
        # 根据事件类型选择日志级别
        if event_type == "ERROR":
            logger.error(f"{event_type}: {message}")
        elif event_type == "WARNING":
            logger.warning(f"{event_type}: {message}")
        elif event_type == "START" or event_type == "STOP":
            logger.info(f"{event_type}: {message}")
        else:
            logger.info(f"{event_type}: {message}")


# ============================================================
# 便捷日志器获取函数
# ============================================================

def get_logger(name: str = None) -> logging.Logger:
    """
    获取通用日志器
    
    Args:
        name: 日志器名称，如果为 None 则返回根日志器
        
    Returns:
        logging.Logger: 日志器
    """
    if name:
        return logging.getLogger(name)
    return logging.getLogger()


def get_enqueue_logger() -> logging.Logger:
    """
    获取入队专用日志器
    
    详细模式下日志会写入 enqueue.log
    用于记录：PLC入队请求、相机数据等
    
    Returns:
        logging.Logger: 入队日志器
    """
    config = _load_logging_config()
    if config.get('log_mode') == 'detailed':
        return logging.getLogger('ENQUEUE')
    return logging.getLogger('PLC')


def get_dequeue_logger() -> logging.Logger:
    """
    获取出队专用日志器
    
    详细模式下日志会写入 dequeue.log
    用于记录：PLC出队请求、机械臂通讯等
    
    Returns:
        logging.Logger: 出队日志器
    """
    config = _load_logging_config()
    if config.get('log_mode') == 'detailed':
        return logging.getLogger('DEQUEUE')
    return logging.getLogger('ARM')


# 预定义的模块日志器获取函数（保持向后兼容）
def get_plc_logger():
    """获取PLC模块专用日志器"""
    return get_logger("PLC")


def get_camera_logger():
    """获取相机模块专用日志器"""
    return get_logger("CAMERA")


def get_arm_logger():
    """获取机械臂模块专用日志器"""
    return get_logger("ARM")


def get_queue_logger():
    """获取队列管理模块专用日志器"""
    return get_logger("QUEUE")


def get_flow_logger():
    """获取流程模块专用日志器"""
    return get_logger("FLOW")


# ============================================================
# 原始通讯日志记录辅助函数
# ============================================================

def log_raw_data(logger: logging.Logger, direction: str, source: str, raw_data: str, extra: str = ""):
    """
    记录原始通讯数据
    
    仅在 detailed 模式下记录，simple 模式下不记录
    
    Args:
        logger: 日志器对象
        direction: 通讯方向，RECV（接收）或 SEND（发送）
        source: 来源/目标标识，如 "192.168.2.1:9999"
        raw_data: 原始数据（十六进制字符串）
        extra: 额外说明信息（可选）
    """
    config = _load_logging_config()
    if config.get('log_mode') != 'detailed':
        return
    
    extra_info = f" {extra}" if extra else ""
    direction_cn = {"RECV": "接收", "SEND": "发送"}.get(direction, direction)
    logger.info(f"{direction_cn} 来源/目标={source} 原始数据={raw_data}{extra_info}")


# ============================================================
# 使用示例
# ============================================================

if __name__ == '__main__':
    # 测试日志系统
    print("=== 测试日志系统 ===")
    
    # 初始化日志系统
    logger_manager = SystemLogger("test_logs")
    
    # 测试不同日志器
    logger = get_logger("TEST")
    enqueue_logger = get_enqueue_logger()
    dequeue_logger = get_dequeue_logger()
    
    logger.info("这是通用日志")
    enqueue_logger.info("这是入队日志")
    dequeue_logger.info("这是出队日志")
    
    # 测试原始数据记录
    log_raw_data(enqueue_logger, "RECV", "192.168.2.1:9999", "01000A0502FFFF", "入队请求")
    log_raw_data(dequeue_logger, "SEND", "192.168.2.10:8081", "314E", "发送数据")
    
    print("\n=== 日志记录完成 ===")
    print("查看 logs 目录下的日志文件")
