---
doc_type: issue-fix
issue: 2026-05-19-console-log-freeze-keypress
path: standard
fix_date: 2026-05-19
related: [console-log-freeze-keypress-analysis.md]
tags: [runtime, logging, windows-console, device-communication]
---

# 终端日志按键后才刷新修复记录

## 1. 实际采用方案

采用分析文档中的方案 A：生产运行保留完整文件日志，终端只显示低频诊断；原始通讯日志不再写终端，避免 PLC / 机械臂通讯线程被 Windows 终端刷新状态阻塞。

同时将主要运行日志文案改为中文，日志级别名也改为中文，方便现场直接阅读日志文件。

## 2. 改动文件清单

- `config/settings.py`
  - 新增 `console_enabled`、`console_level`、`raw_log_to_console` 配置。
  - 默认终端级别调整为 `WARNING`，原始通讯日志默认只写文件。
  - 日志格式改为使用中文级别字段 `levelname_cn`。
- `src/utils/logger.py`
  - 新增中文日志级别显示。
  - 按配置创建终端 handler。
  - detailed 模式下 `ENQUEUE` / `DEQUEUE` 只写专用文件日志，默认不写终端。
  - 设置 `ENQUEUE` / `DEQUEUE` 的 `propagate=False`，避免重复传播到 root logger。
  - 原始通讯日志文案改为中文。
- `src/main.py`
  - 主程序、监控、启动提示和异常输出改为中文。
- `src/core/plc_handler.py`
  - PLC 入队、出队、反馈相关日志改为中文。
- `src/core/arm_controller.py`
  - 机械臂连接、收发、断开相关日志改为中文。
- `src/core/camera_server.py`
  - 相机服务启动、连接、发送、断开相关日志改为中文。
- `src/core/enqueue_workflow.py`
  - 入队流程、2D/3D 相机流程日志改为中文。
- `src/core/queue_manager.py`
  - 队列入队、出队、缓存、超时、状态变化日志改为中文。
- `src/web/dashboard_server.py`
  - HMI 服务日志改为中文。

## 3. 验证结果

- `python -m compileall config src`：通过。
- 日志系统运行检查：通过。
  - root handler 为 `FileHandler(DEBUG)` + `StreamHandler(WARNING)`。
  - `ENQUEUE` handler 为 `FileHandler(DEBUG)`，`propagate=False`。
  - `DEQUEUE` handler 为 `FileHandler(DEBUG)`，`propagate=False`。
  - 调用 `log_raw_data()` 后，原始通讯日志写入 `logs/enqueue_20260519.log`，未输出到终端。
  - 文件日志示例已显示中文级别和中文文案。
- `python .codestable/tools/validate-yaml.py --dir .codestable/issues/2026-05-19-console-log-freeze-keypress`：通过。
- `python -m pytest tests`：未通过，20 failed / 37 passed。
  - 失败集中在旧的 PLC 3D 短帧、队列 pending slot、相机数据归队、相机触发协议等业务规则。
  - 本次修改未触碰这些业务逻辑文件的相关路径，失败看起来是现有测试与当前业务代码状态不一致，需另行处理。

## 4. 遗留事项

- 需要在现场或设备模拟器环境复测 report 中的步骤：不按键盘时终端不再承载原始通讯日志，设备通讯应继续推进。
- 如果关闭终端原始日志后仍出现卡住，再按 analysis 第 6 节继续排查 socket 阻塞或 Windows 控制台 QuickEdit / Mark 状态。
