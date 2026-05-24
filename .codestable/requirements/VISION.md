---
doc_type: requirements-index
status: current
last_reviewed: 2026-05-24
---

# 能力愿景索引

## Current

- `cpp-minimal-loop`：C++ 主控先跑通 PLC 双端口、队列、fake robot 和 `XN/XD` 最小闭环。
- `camera-flow-parity`：复刻 legacy 单相机和 dual camera 入队流程，保持 PLC/相机外部协议不变。
- `duco-sdk-adapter`：封装新松 DUCO 远程 API 的连接、心跳、上电使能、状态和任务控制入口。
- `duco-task-executor`：把队列 payload 转成 DUCO 运动段并执行，按结果回 PLC `XN/XD`。
- `qt-hmi-shell`：给 Qt Widgets 工业 HMI 提供页面、快照模型和命令边界蓝图。
- `modbus-plc-reservation`：预留默认禁用的 Smart200 Modbus TCP 客户端、地址表和读写 API。
- `diagnostics-and-logs`：让现场联调可以按时间、设备、count/pointer 和错误码追踪通讯与异常。

## Draft

- 暂无。

## Outdated

- 暂无。
