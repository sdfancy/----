---
doc_type: issue-analysis
issue: 2026-05-19-console-log-freeze-keypress
status: confirmed
root_cause_type: concurrency
related: [console-log-freeze-keypress-report.md]
tags: [runtime, logging, windows-console, device-communication]
---

# 终端日志按键后才刷新根因分析

## 1. 问题定位

| 关键位置 | 说明 |
|---|---|
| `config/settings.py:237` | 当前默认 `log_mode` 为 `detailed`，通讯原始数据会被记录。 |
| `src/utils/logger.py:216` | detailed 模式创建控制台 `StreamHandler`，所有 INFO 及以上日志同步写终端。 |
| `src/utils/logger.py:235` | root logger 同时挂 system 文件 handler 和控制台 handler。 |
| `src/utils/logger.py:238` | `ENQUEUE` 专用 logger 额外挂 enqueue 文件 handler 和控制台 handler。 |
| `src/utils/logger.py:245` | `DEQUEUE` 专用 logger 额外挂 dequeue 文件 handler 和控制台 handler。 |
| `src/utils/logger.py:410` | `log_raw_data()` 用 `logger.info()` 记录每次原始通讯数据。 |
| `src/core/plc_handler.py:238` | PLC 入队接收线程在处理回调前同步写原始通讯日志。 |
| `src/core/plc_handler.py:266` | PLC 出队接收线程在处理回调前同步写原始通讯日志。 |
| `src/core/plc_handler.py:355` | 向 PLC 发送反馈后同步写原始通讯日志。 |
| `src/core/arm_controller.py:370` | 机械臂接收线程在解析命令前同步写原始通讯日志。 |
| `src/core/arm_controller.py:459` | 向机械臂发送数据前同步写原始通讯日志。 |
| `src/main.py:450` | 主循环监控日志也同步写控制台，说明控制台输出在主线程和通讯线程之间共享。 |

补充验证：运行 `SystemLogger()` 后，root logger 有 `FileHandler + StreamHandler`；`ENQUEUE` 和 `DEQUEUE` 各有 `FileHandler + StreamHandler`，且 `propagate=True`。因此专用通讯日志会先写一次专用控制台 handler，再传播到 root 后再写一次控制台 handler。

## 2. 失败路径还原

**正常路径**：PLC / 机械臂发送数据 → 对应网络线程 `recv()` 收到报文 → 记录通讯日志 → 业务回调解析报文 → 队列状态更新 → 发送相机 / PLC / 机械臂反馈 → 写入文件日志和必要控制台日志。

**失败路径**：PLC / 机械臂发送数据 → 网络线程收到报文 → 在 `log_raw_data()` 或业务 `logger.info()` 处同步写 Windows 终端 → 终端窗口输出阻塞或刷新停住 → 当前网络线程卡在日志 handler 的 `write/flush` 上 → 后续回调、发送反馈和队列推进延后 → 现场表现为日志和设备通讯都卡住；按住键盘后终端恢复刷新，阻塞的日志一次性输出，积压的通讯处理继续执行。

**分叉点**：`src/utils/logger.py:216` 和 `src/utils/logger.py:238` — 日志系统把通讯热路径上的 INFO 日志同步写到终端；同时 `ENQUEUE` / `DEQUEUE` 没有关闭传播，导致每条原始通讯日志至少两次触达控制台。

## 3. 根因

**根因类型**：concurrency

**根因描述**：设备通讯线程和控制台输出被同步耦合在一起。当前 detailed 日志模式下，PLC、机械臂的收发线程在真正处理业务前后都会写 INFO 日志；这些 INFO 日志不仅写文件，也同步写 Windows 终端。Windows 终端一旦进入暂停、选择、刷新阻塞或输出变慢状态，`StreamHandler` 会阻塞当前线程。由于这个线程正是收包、解析、反馈的线程，日志输出阻塞会直接表现成设备通讯卡住。按键让终端恢复刷新后，阻塞释放，所以之前积压的日志和通讯处理会集中发生。

**是否有多个根因**：是。

- 主因：通讯热路径同步写控制台，控制台状态会反向阻塞业务线程。
- 次因：`ENQUEUE` / `DEQUEUE` 专用 logger 同时有自己的控制台 handler 且继续向 root 传播，放大了控制台写入次数。
- 次因：`LOGGING_CONFIG['log_level']` 目前没有驱动 handler 级别，控制台级别被硬编码为 INFO，现场运行时无法只靠配置降低终端输出压力。

## 4. 影响面

- **影响范围**：不只影响报告中的终端显示；所有 PLC 入队、PLC 出队、机械臂收发、相机发送以及队列关键 INFO 日志，只要经过 root 控制台 handler，都可能被同一个终端阻塞拖住。
- **潜在受害模块**：`plc_handler`、`arm_controller`、`camera_server`、`queue_manager`、`enqueue_workflow`、`main` 监控循环。
- **数据完整性风险**：有。阻塞期间队列不一定写坏，但设备侧可能等待反馈、超时、重发或错过节拍，进而造成现场节拍与软件处理时序偏移。
- **严重程度复核**：维持 P1。核心通讯受损，但目前按键可临时恢复，且根因集中在日志/终端耦合，不是队列数据结构已损坏。

## 5. 修复方案

### 方案 A：生产运行默认文件日志，控制台只做低频诊断

- **做什么**：在 `LOGGING_CONFIG` 增加 `console_enabled` / `console_level` / `raw_log_to_console` 一类配置；`logger.py` 按配置决定是否添加控制台 handler。detailed 模式下，`ENQUEUE` / `DEQUEUE` 只写各自文件，设置 `propagate=False`，避免原始通讯日志重复打到控制台。生产默认关闭 raw 通讯控制台输出，必要时只保留 WARNING 及以上控制台输出。
- **优点**：改动集中在 `config/settings.py` 和 `src/utils/logger.py`；直接切断通讯热路径对终端刷新状态的依赖；现场仍保留完整文件日志。
- **缺点 / 风险**：终端不再实时显示每条通讯原始报文，调试时需要看日志文件或显式打开配置。
- **影响面**：主要影响日志输出方式，不改 PLC、相机、机械臂业务协议。

### 方案 B：改为异步日志队列，业务线程只入队不直接写终端

- **做什么**：用 `logging.handlers.QueueHandler` / `QueueListener` 包装文件和控制台 handler；网络线程只把日志记录放进队列，由单独日志线程写文件和终端。
- **优点**：即使控制台短暂阻塞，设备通讯线程也不直接卡在 `StreamHandler.write()` 上；调试时仍可保留控制台输出。
- **缺点 / 风险**：改动更大，需要处理队列容量、退出 flush、日志线程异常和打包运行行为；如果控制台长期阻塞，日志队列仍可能积压。
- **影响面**：影响整个日志系统，需要更完整的回归测试。

### 方案 C：Windows 启动时禁用 QuickEdit / 终端暂停类输入模式

- **做什么**：在主程序启动时用 Windows API 关闭控制台 QuickEdit，减少鼠标选中或控制台暂停导致的进程输出阻塞。
- **优点**：改动小，直接针对常见 Windows 控制台暂停触发条件。
- **缺点 / 风险**：只处理一种环境触发，无法解决控制台输出慢、远程终端刷新慢、日志量过大或 handler 重复输出；如果程序通过不同终端或重定向运行，效果不稳定。
- **影响面**：只影响 Windows 控制台行为，不影响业务代码。

### 推荐方案

**推荐方案 A**。理由：这个问题的工程根因不是某一个按键，而是“通讯线程依赖终端同步输出”。生产现场应以文件日志为准，终端只作为低频诊断窗口。方案 A 改动范围最小，能直接消除 raw 通讯日志对控制台的依赖，同时保留完整落盘日志，副作用可控。方案 B 可以作为后续增强，但这次修 P1 问题不需要先引入更复杂的异步日志框架。

## 6. 网络资料补充：其他可能原因

这次补充查了 Windows 控制台、Python logging、Python 标准输出缓冲、Python socket 行为。结论是：当前代码证据仍然最匹配“控制台同步日志阻塞通讯线程”，但现场还应按下面顺序排查。

### A. Windows 控制台 QuickEdit / 选择模式

- Microsoft `SetConsoleMode` 文档说明 `ENABLE_QUICK_EDIT_MODE` 允许用户在控制台窗口中选择和编辑文本；关闭时需要保留 `ENABLE_EXTENDED_FLAGS` 但不带 `ENABLE_QUICK_EDIT_MODE`。
- Microsoft Old New Thing 也提到 QuickEdit 让控制台长期处于类似 Mark mode 的状态，并会改变控制台程序对鼠标输入的处理。
- 这类问题能解释“用户交互后终端刷新/程序继续”的现象，但它本身不是业务代码根因；业务侧仍不应让通讯线程依赖终端输出。

### B. Python stdout / stderr 缓冲

- Python 文档说明交互式 stdout 是行缓冲，非交互式 stdout 会像普通文件一样块缓冲；stderr 通常是行缓冲；`-u` 或 `PYTHONUNBUFFERED` 可改成非缓冲。
- 这能解释“日志显示延迟”，尤其是输出被重定向时集中出现。
- 但本项目的 `logging.StreamHandler` 会调用 stream 的 `write()` 和 `flush()`，所以单纯 stdout 缓冲不足以解释“设备通讯也卡住”；更像是日志 handler 的 I/O 阻塞了通讯线程。

### C. Python logging 同步 handler

- Python `StreamHandler` 官方说明会写入 stream，并调用 stream 的 `flush()`。
- Python `QueueHandler` / `QueueListener` 官方说明适合让 handler 在独立线程中工作，避免服务线程被慢 I/O 拖住。
- 这与当前代码最吻合：PLC/机械臂线程在每次收发数据时同步调用日志，控制台慢或阻塞时，通讯线程也被拖住。

### D. Python socket / 网络收发本身阻塞

- Python socket 文档说明 `recv()` 从 socket 接收数据，返回空 bytes 表示对端断开；`sendall()` 会持续发送直到全部发完或出错。
- 本项目确实有多处阻塞式 `recv()` / `sendall()`，网络或对端异常时也可能卡住。
- 但键盘按键不应该解除 socket 阻塞；如果按键能稳定触发“积压日志和通讯一起继续”，socket 本身更像次要风险，不是首要根因。

### E. `requests` / HTTP 请求模块

- 本仓库搜索未发现 `requests` / `httpx` 参与主程序通讯；当前通讯主要是 Python `socket`。
- 因此“Python requests 发送请求模块问题”不适用于当前主流程。

### 现场验证顺序

1. 先用 `scripts/start_main.ps1` 或命令行重定向运行，把 stdout/stderr 写入文件；如果问题消失，基本确认是终端/控制台输出路径。
2. 临时关闭控制台日志或只保留 WARNING 以上；如果通讯恢复，确认是同步控制台 logging。
3. 检查 Windows 控制台 QuickEdit / Mark / 选择状态，必要时手动关闭 QuickEdit 或代码里关闭。
4. 对比日志文件时间戳：如果文件日志持续增长但终端不刷新，是显示/缓冲问题；如果文件日志也停止，是 logging handler 或通讯线程被阻塞。
5. 如果关闭控制台输出后仍卡，再单独给 socket `recv()` / `sendall()` 增加超时和状态日志，排查对端设备或网络阻塞。
