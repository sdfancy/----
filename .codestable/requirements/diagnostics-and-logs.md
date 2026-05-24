---
doc_type: requirement
slug: diagnostics-and-logs
pitch: 让现场联调可以按时间、设备和 count 追踪每一步通讯与异常
status: current
created: 2026-05-24
last_reviewed: 2026-05-24
tags: [diagnostics, logs, simulator, health]
---

# 诊断日志与设备模拟能力

## 1. 能力愿景

新 C++ 喷涂控制项目需要把 PLC、相机、机械臂、Modbus 和队列状态串成可追踪的事件历史。现场出现“PLC 已发但软件没反应”“相机回了但队列没写”“机械臂没执行”这类问题时，联调人员应能按时间、设备、count、pointer 和错误码快速定位断点，而不是只靠控制台滚屏。

## 2. 用户故事

- 作为 PLC 联调人员，我可以查看 PLC `9999/9090` 原始收发帧、解析结果和反馈发送结果，确认问题在 PLC 侧还是软件侧。
- 作为相机联调人员，我可以查看 2D/3D/legacy 相机触发、READY、payload 和 Done 事件，确认 count/pointer 是否匹配。
- 作为机械臂维护人员，我可以查看任务 accepted/finished、DUCO warning、最近错误和设备健康状态。
- 作为软件开发人员，我可以用本地模拟器复现 PLC、相机和 fake robot 闭环，生成可复查的日志证据。

## 3. 边界

- 不改变 PLC `9999/9090`、相机 legacy/dual camera、DUCO motion 或 Modbus 的业务流程。
- 不做 Qt HMI 页面；本能力只提供日志、快照和模拟器基础，HMI 后续消费这些数据。
- 不把原始通讯日志继续刷到控制台作为主要诊断方式；控制台只保留关键启动和错误提示。
- 不实现真实硬件验收脚本；硬件联调 checklist 留给 `hardware-acceptance`。
- 日志不得在 socket 回调或 robot worker 中做长时间阻塞写入。

## 4. 验收依据

- 事件历史能表达时间、等级、分类、设备、方向、payload、count、pointer、错误码和消息。
- 原始 PLC/相机收发帧能落入持久化日志，且可配置是否开启。
- 设备健康快照能展示 PLC、相机、robot、Modbus 的连接数、在线状态、最近收发和最近错误。
- 测试模拟器能复现最小 PLC 出队闭环和 dual camera 入队流程。
- 反向核对确认外部通讯协议和队列/DUCO 执行语义不变。

## 5. 变更日志

- 2026-05-24：起草 draft，关联 roadmap item `diagnostics-and-logs`。
- 2026-05-24：实现 current，落地 `DiagnosticEvent`、`DiagnosticsService`、`DeviceHealthRegistry`、`RawFrameFileSink` 和 `DeviceSimulator`；保持 PLC、相机、DUCO、Modbus 外部流程不变。
