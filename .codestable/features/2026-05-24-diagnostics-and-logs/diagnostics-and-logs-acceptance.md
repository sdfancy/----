---
doc_type: feature-acceptance
feature: 2026-05-24-diagnostics-and-logs
status: accepted
summary: 验收诊断事件、原始日志、设备健康快照和本地模拟器
tags: [diagnostics, logs, simulator, health]
---

# diagnostics-and-logs 验收报告

> 阶段：阶段 3（验收闭环）
> 验收日期：2026-05-24
> 关联方案 doc：`.codestable/features/2026-05-24-diagnostics-and-logs/diagnostics-and-logs-design.md`

## 1. 接口契约核对

**接口示例逐项核对**
- [x] `diagnostics.recordRawFrame("plc.enqueue", "rx", bytes, {count, pointer})`：实际接口为 `DiagnosticsService::recordRawFrame(category, device, direction, payload, count, pointer, connectionCount)`，覆盖 PLC raw frame 的 device、direction、payloadHex、count、pointer。
- [x] `eventLog.query(EventFilter{.device = "plc", .count = 10})`：`EventFilter` 支持 device/count/pointer/level/category/time window；`DiagnosticsEventLogTest::appendsAndQueriesStructuredEvents` 覆盖。
- [x] `healthRegistry.snapshot("camera.3d")`：`DeviceHealthRegistry::snapshot(device)` 与 `snapshots()` 已落地；`DiagnosticsServiceTest::rawFramesUpdateEventsAndHealth` 覆盖。
- [x] `simulator.runDualCameraCycle(10, 1)`：实际接口接收 PLC/camera 配置、count、pointer 和反馈输出；`DeviceSimulatorTest::runsDualCameraCycleThroughPublicPorts` 覆盖。

**名词层“现状 → 变化”逐项核对**
- [x] `DiagnosticEvent` 包含 timestamp、level、category、device、direction、payloadHex、count、pointer、code、message。
- [x] `EventLog` 已从简单 append/records 扩展为 bounded ring buffer、`append(DiagnosticEvent)`、`query(filter)`、`clear()`、`droppedCount()`。
- [x] `DiagnosticCode` 已提供 PLC、camera、robot、modbus、log write 和 event dropped 诊断码。
- [x] `DeviceHealthRegistry` 已记录 online、connectionCount、lastRxAt、lastTxAt、lastErrorAt、lastError。
- [x] `DiagnosticsService`、`RawFrameFileSink`、`DeviceSimulator` 均有独立文件和测试落点。

**流程图核对**
- [x] `PLC/Camera/Robot signals -> DiagnosticsService -> EventLog + DeviceHealthRegistry + RawFrameFileSink -> Application events/health` 均有代码落点。

## 2. 行为与决策核对

**需求摘要逐项验证**
- [x] 事件历史可按设备、count、pointer 查询：`DiagnosticsEventLogTest` 和 `DeviceSimulatorTest` 覆盖。
- [x] 原始通讯日志可持久化：`DiagnosticsServiceTest::writesConfiguredLogsOnFlush` 验证 `raw-frames.jsonl` 和 `events.jsonl`。
- [x] 设备健康快照可读：`Application::deviceHealthSnapshot()` 暴露只读入口。
- [x] 本地模拟器可复现 PLC 最小闭环和 dual camera 入队：`DeviceSimulatorTest` 两个集成场景通过。

**明确不做逐项核对**
- [x] 未修改 PLC `9999/9090` 字节格式、反馈码或端口默认值：`src/protocol`、`PlcEndpoint.cpp` 无 diff。
- [x] 未修改相机 READY/2D/3D/Done 外部协议：`CameraEndpoint.cpp` 和 protocol camera 无 diff。
- [x] diagnostics 未调用 `QueueManager` 写接口或 DUCO motion：grep `QueueManager|enqueueTask|moveJPose2|moveL|setToolDigitalOut|setStandardDigitalOut` 在 `src/diagnostics` 无命中。
- [x] 未新增 Qt Widgets 页面或 HMI 日志模型：grep `QWidget|QMainWindow|Qt6::Widgets` 无命中。
- [x] socket 回调中不直接阻塞写文件：Application 只调用 `DiagnosticsService`，文件写入由 `RawFrameFileSink` 队列和 `QTimer` flush 承担。

**关键决策落地**
- [x] D1 诊断集中但流程不集中：`DiagnosticsService` 只收集事件和健康状态，不接管 PLC、camera、robot 编排。
- [x] D2 事件 schema 先行：`DiagnosticEvent` 和 `DiagnosticCode` 先落地，再由 service/sink 消费。
- [x] D3 raw frame 持久化可配置：`LoggingConfig` 提供 `persistRawFrames`、`persistEvents`、`logDir`、`flushIntervalMs`。
- [x] D4 健康状态来自信号：PLC/camera rawFrame、warning 和 robot status/warning 均写入 diagnostics。
- [x] D5 模拟器不嵌入生产主控：`DeviceSimulator` 只在 diagnostics 测试支持中通过公开 TCP 端口运行。

**挂载点反向核对**
- [x] 配置挂载：`[logging]` 已新增 `log_dir`、`persist_raw_frames`、`persist_events`、`max_in_memory_events`、`flush_interval_ms`。
- [x] 应用诊断挂载：`Application` 用 `DiagnosticsService` 替代分散 `EventLog` wiring。
- [x] 快照挂载：`Application::events(filter)`、`deviceHealthSnapshot()`、`flushDiagnostics()` 已暴露。
- [x] 文件日志挂载：`RawFrameFileSink` 和日志文件路径已独立。
- [x] 模拟器挂载：`DeviceSimulator` 和对应测试已加入 CMake。
- [x] 反向 grep：本 feature 引用集中在 diagnostics、Application、config、domain snapshot、tests、CMake 和文档清单内。
- [x] 拔除沙盘：移除 diagnostics 新文件、Application diagnostics wiring、logging 配置和测试后，诊断能力消失；PLC/相机/DUCO 主流程仍保留。

## 3. 验收场景核对

- [x] S1 PLC raw frame：`DeviceSimulatorTest::runsMinimalPlcLoopThroughPublicPorts` 通过公开端口发送 enqueue/dequeue；事件可按 `plc.enqueue + count=10` 查询，feedback 事件包含 `1D`。
- [x] S2 协议错误：PLC enqueue/dequeue parse 失败时写 `PLC_PROTOCOL_INVALID_FRAME` warning 并继续监听；既有 application loop 和 protocol 测试全量通过。
- [x] S3 设备健康：`DiagnosticsServiceTest::rawFramesUpdateEventsAndHealth`、`warningsUpdateEventsAndHealth` 覆盖 lastRx、connectionCount、lastError；Modbus 当前仍是 sidecar 预留，由 `DiagnosticCode::modbusError()` 和通用 warning 入口承载。
- [x] S4 文件日志：`writesConfiguredLogsOnFlush` 验证 raw/event 文件生成；`disabledRawPersistenceDoesNotWriteRawFile` 验证关闭 raw 后不生成 raw 文件。
- [x] S5 容量上限：`DiagnosticsEventLogTest::appliesCapacityLimitAndDroppedCount` 验证旧事件丢弃和 dropped 计数。
- [x] S6 模拟器最小闭环：PLC 收到 `1N1D`，事件链包含 enqueue、dequeue、robot dispatch、feedback。
- [x] S7 模拟器 dual camera：PLC 收到 `Done`，队列写入 count/pointer 匹配的 camera payload，事件链包含 camera raw 与 enqueue feedback。
- [x] 构建与测试：ASCII 镜像构建通过，17 个 QtTest 组总计 122 passed / 0 failed。

## 4. 术语一致性

- [x] `DiagnosticEvent`、`EventFilter`、`DiagnosticCode`、`DiagnosticsService`、`DeviceHealthRegistry`、`RawFrameFileSink`、`DeviceSimulator` 命名与方案一致。
- [x] `DeviceHealth` 在代码中对应 `domain::DeviceHealthSnapshot` 和 `DeviceHealthRegistry`，职责一致。
- [x] 防冲突 grep：diagnostics 目录无 `QueueManager` 写接口、DUCO motion 调用和 Qt Widgets 命中。

## 5. 架构归并

- [x] `.codestable/architecture/ARCHITECTURE.md` 术语表已补 DiagnosticEvent、RawFrameLog、DeviceHealth、DiagnosticsService、DeviceSimulator。
- [x] `.codestable/architecture/ARCHITECTURE.md` 已新增 `3.6 已落地诊断日志基础`，说明事件历史、文件日志、健康快照、模拟器和 Application 只读入口。
- [x] `.codestable/architecture/ARCHITECTURE.md` 已补硬边界：diagnostics 不调用队列写接口或 DUCO motion，文件持久化不得阻塞 socket 回调。

## 6. requirement 回写

- [x] `.codestable/requirements/diagnostics-and-logs.md` 已从 `draft` 升级为 `current`。
- [x] 保留能力愿景和边界，追加 current 实现变更日志。
- [x] `.codestable/requirements/VISION.md` 已把 `diagnostics-and-logs` 从 Draft 移到 Current。

## 7. roadmap 回写

- [x] `cpp-spray-control-items.yaml` 中 `diagnostics-and-logs` 已从 `in-progress` 改为 `done`。
- [x] `cpp-spray-control-roadmap.md` 第 5 节已同步标记 done 和 feature 目录。
- [x] roadmap 变更日志已追加 diagnostics 完成记录。

## 8. attention.md 候选盘点

- [x] 无新增候选：中文路径 Qt moc 和 MinGW PATH 已在 attention.md 记录；本 feature 未暴露新的每次必读环境坑。

## 9. 遗留

- 后续优化点：真实 HMI 日志表格、分页查询、导出、日志保留策略和真实硬件联调 checklist 另走后续 feature。
- 已知限制：Modbus 当前仍是默认禁用 sidecar，Application 未创建 Modbus 生命周期；后续 HMI/规则引擎接入 Modbus 时再把其 warning 连接到 diagnostics。
- 顺手发现：`tests/` 继续平铺，后续设备集成测试增多时可另走 `cs-refactor` 评估 `tests/support` 分组。
