"""
异常处理模块 - 增强版
定义工业自动化系统的自定义异常和警告
"""

import logging
from dataclasses import dataclass
from typing import Optional

# 配置日志
logger = logging.getLogger(__name__)


@dataclass
class ProcessedArmData:
    """兼容旧测试的数据结构定义"""
    flag: int
    sensor_count: int
    pulse_pointer: int
    points: list

class IndustrialBaseException(Exception):
    """工业自动化系统基础异常类"""
    def __init__(self, message: str, module: str = "SYSTEM", error_code: str = None):
        self.message = message
        self.module = module
        self.error_code = error_code
        super().__init__(self.message)
    
    def log_error(self):
        """记录错误日志"""
        error_info = f"[{self.module}] {self.message}"
        if self.error_code:
            error_info += f" (错误代码: {self.error_code})"
        logger.error(error_info)

# ==================== 连接异常类 ====================
class ConnectionWarning(Warning):
    """连接警告类 - 仅记录日志，不中断程序执行"""
    def __init__(self, message: str, module: str = "CONNECTION"):
        self.message = message
        self.module = module
        logger.warning(f"[{self.module}] 连接警告: {self.message}")

class PLCConnectionWarning(ConnectionWarning):
    """PLC连接警告"""
    def __init__(self, message: str):
        super().__init__(message, "PLC")

class CameraConnectionWarning(ConnectionWarning):
    """相机连接警告"""
    def __init__(self, message: str):
        super().__init__(message, "CAMERA")

class ArmConnectionWarning(ConnectionWarning):
    """机械臂连接警告"""
    def __init__(self, message: str):
        super().__init__(message, "ARM")

# ==================== 数据异常类 ====================
class DataWarning(Warning):
    """数据警告类 - 记录日志并可选择使用默认值继续处理"""
    def __init__(self, message: str, module: str = "DATA", use_default: bool = True):
        self.message = message
        self.module = module
        self.use_default = use_default
        logger.warning(f"[{self.module}] 数据警告: {self.message}")
        
        if self.use_default:
            logger.info(f"[{self.module}] 使用默认值继续处理")

class PLCDataWarning(DataWarning):
    """PLC数据警告"""
    def __init__(self, message: str, use_default: bool = True):
        super().__init__(message, "PLC", use_default)

class CameraDataWarning(DataWarning):
    """相机数据警告"""
    def __init__(self, message: str, use_default: bool = True):
        super().__init__(message, "CAMERA", use_default)

class ArmDataWarning(DataWarning):
    """机械臂数据警告"""
    def __init__(self, message: str, use_default: bool = True):
        super().__init__(message, "ARM", use_default)

# ==================== 队列异常类 ====================
class QueueWarning(Warning):
    """队列警告类 - 针对队列操作的各种异常情况"""
    def __init__(self, message: str, queue_type: str = "QUEUE"):
        self.message = message
        self.queue_type = queue_type
        logger.warning(f"[{self.queue_type}] 队列警告: {self.message}")

class QueueOverflowWarning(QueueWarning):
    """队列溢出警告 - 系统会自动处理（环形队列机制）"""
    def __init__(self, message: str = "队列已满，将覆盖最旧数据"):
        super().__init__(message, "QUEUE_OVERFLOW")

class QueueDataWarning(QueueWarning):
    """队列数据异常警告 - 使用默认值处理"""
    def __init__(self, message: str = "队列数据异常，使用默认值"):
        super().__init__(message, "QUEUE_DATA")
        logger.info("[QUEUE_DATA] 已启用默认值填充机制")

class QueuePointerWarning(QueueWarning):
    """队列指针异常警告 - 严格按照PLC发送的指针执行操作"""
    def __init__(self, message: str = "队列指针异常，按PLC指针强制执行"):
        super().__init__(message, "QUEUE_POINTER")
        logger.warning("[QUEUE_POINTER] 系统将严格遵循PLC指针指令")

# ==================== 双端口PLC异常类 ====================
class DualPortPLCException(Exception):
    """双端口PLC异常类"""
    def __init__(self, message: str, port_type: str = "UNKNOWN"):
        self.message = message
        self.port_type = port_type
        super().__init__(self.message)
        logger.error(f"[DUAL_PORT_PLC] {port_type}端口异常: {message}")

class EnqueuePortException(DualPortPLCException):
    """入队端口异常"""
    def __init__(self, message: str):
        super().__init__(message, "ENQUEUE")

class DequeuePortException(DualPortPLCException):
    """出队端口异常"""
    def __init__(self, message: str):
        super().__init__(message, "DEQUEUE")

class PortConflictException(DualPortPLCException):
    """端口冲突异常"""
    def __init__(self, message: str = "入队和出队端口号不能相同"):
        super().__init__(message, "PORT_CONFLICT")

# ==================== 出队机制异常类 ====================
class DequeueMechanismException(Exception):
    """出队机制异常类"""
    def __init__(self, message: str, mechanism_type: str = "UNKNOWN"):
        self.message = message
        self.mechanism_type = mechanism_type
        super().__init__(self.message)
        logger.error(f"[DEQUEUE_MECHANISM] {mechanism_type}异常: {message}")

class PointerChangeException(DequeueMechanismException):
    """指针变动异常"""
    def __init__(self, message: str):
        super().__init__(message, "POINTER_CHANGE")

class CacheManagementException(DequeueMechanismException):
    """缓存管理异常"""
    def __init__(self, message: str):
        super().__init__(message, "CACHE_MANAGEMENT")

class ArmRequestException(DequeueMechanismException):
    """机械臂请求异常"""
    def __init__(self, message: str):
        super().__init__(message, "ARM_REQUEST")

# ==================== 异常处理工具函数 ====================
class ExceptionHandler:
    """异常处理器 - 提供统一的异常处理和日志记录功能"""
    
    @staticmethod
    def handle_connection_exception(exception: Exception, module: str) -> bool:
        """处理连接异常"""
        logger.warning(f"[{module}] 连接异常: {str(exception)}")
        # 连接异常仅记录日志，不中断执行
        return True
    
    @staticmethod
    def handle_data_exception(exception: Exception, module: str, 
                            default_value=None) -> any:
        """处理数据异常"""
        logger.error(f"[{module}] 数据异常: {str(exception)}")
        
        if default_value is not None:
            logger.info(f"[{module}] 使用默认值继续处理")
            return default_value
        return None
    
    @staticmethod
    def handle_queue_exception(exception: Exception, queue_operation: str) -> bool:
        """处理队列异常"""
        if isinstance(exception, QueueOverflowWarning):
            logger.warning(f"[QUEUE] 溢出处理: {queue_operation}")
            return True
            
        elif isinstance(exception, QueueDataWarning):
            logger.warning(f"[QUEUE] 数据异常处理: {queue_operation}")
            return True
            
        elif isinstance(exception, QueuePointerWarning):
            logger.error(f"[QUEUE] 指针异常强制执行: {queue_operation}")
            return True
            
        else:
            logger.error(f"[QUEUE] 未知异常: {str(exception)}")
            return False
    
    @staticmethod
    def handle_dual_port_exception(exception: Exception, operation: str) -> bool:
        """处理双端口PLC异常"""
        if isinstance(exception, PortConflictException):
            logger.error(f"[DUAL_PORT] 端口冲突: {operation}")
            return False
        elif isinstance(exception, (EnqueuePortException, DequeuePortException)):
            logger.error(f"[DUAL_PORT] 端口异常: {operation} - {str(exception)}")
            return True  # 端口异常通常可以恢复
        else:
            logger.error(f"[DUAL_PORT] 未知异常: {str(exception)}")
            return False
    
    @staticmethod
    def handle_dequeue_exception(exception: Exception, operation: str) -> bool:
        """处理出队机制异常"""
        if isinstance(exception, (PointerChangeException, CacheManagementException)):
            logger.warning(f"[DEQUEUE] 可恢复异常: {operation} - {str(exception)}")
            return True
        elif isinstance(exception, ArmRequestException):
            logger.error(f"[DEQUEUE] 机械臂请求异常: {operation} - {str(exception)}")
            return False
        else:
            logger.error(f"[DEQUEUE] 未知异常: {str(exception)}")
            return False

# ==================== 使用示例和测试 ====================
if __name__ == '__main__':
    print("=== 异常处理模块测试 ===")
    
    # 测试各种异常
    print("\n1. 连接警告测试:")
    PLCConnectionWarning("PLC连接超时")
    CameraConnectionWarning("相机响应延迟")
    ArmConnectionWarning("机械臂1未连接")
    
    print("\n2. 数据警告测试:")
    PLCDataWarning("PLC数据格式异常")
    CameraDataWarning("相机数据丢失", use_default=True)
    ArmDataWarning("机械臂反馈数据错误", use_default=False)
    
    print("\n3. 队列警告测试:")
    QueueOverflowWarning()
    QueueDataWarning()
    QueuePointerWarning()
    
    print("\n4. 双端口PLC异常测试:")
    try:
        raise PortConflictException()
    except PortConflictException as e:
        handled = ExceptionHandler.handle_dual_port_exception(e, "端口初始化")
        print(f"端口冲突处理结果: {handled}")
    
    print("\n5. 出队机制异常测试:")
    try:
        raise PointerChangeException("指针变动检测失败")
    except PointerChangeException as e:
        handled = ExceptionHandler.handle_dequeue_exception(e, "指针检测")
        print(f"指针异常处理结果: {handled}")
    
    print("\n=== 异常处理模块测试完成 ===")
