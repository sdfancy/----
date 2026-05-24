---
doc_type: feature-acceptance
feature: 2026-05-23-camera-flow-parity
status: accepted
accepted_at: 2026-05-24
summary: 相机入队 parity 已实现并验收通过
tags: [camera, plc, queue, parity]
---

# camera-flow-parity 验收报告

> 阶段：阶段 3（验收闭环）
> 验收日期：2026-05-24
> 关联方案 doc：.codestable/features/2026-05-23-camera-flow-parity/camera-flow-parity-design.md

## 1. 接口契约核对

**接口示例逐项核对**：
- [x] `parse2dFrame("READY")`：`src/protocol/CameraProtocol.cpp` 返回 `Parsed2dFrameKind::Ready`，测试 `CameraProtocolTest::parsesReadyFrames` 通过。
- [x] `parse2dFrame("10,01,02")`：返回 `count=10`、`partType="01,02"`，测试 `parses2dResultFrames` 通过。
- [x] `drain3dFrames("(1001,10,x)EA(2001,10,y)E")`：完整帧出队，半包保留，测试 `drains3dCompleteFrames` / `keeps3dHalfPacket` 通过。
- [x] `parse3dSegment("(1001,10,x)E")`：返回 `armId=1,count=10,payload`，测试 `parses3dSegments` 通过。

**名词层变化核对**：
- [x] `CameraConfig` 已落在 `src/config/AppConfig.h`，包含 dual/legacy 模式、端口、匹配模式、count 提取模式。
- [x] `Parsed2dFrame`、`Parsed3dSegment`、`FrameDrainResult` 已落在 `src/protocol/CameraProtocol.h`。
- [x] `EnqueueCycle` 在 `src/core/EnqueueWorkflow.h` 内部承载 dual 状态；legacy 等待槽在 `LegacyCameraWorkflow` 内部承载。

**流程图核对**：
- [x] `Application -> PlcEndpoint -> EnqueueWorkflow -> CameraEndpoint -> QueueManager -> PlcEndpoint` 均有代码落点。

## 2. 行为与决策核对

**需求摘要逐项验证**：
- [x] dual 模式复现 `11 -> READY -> 12 -> 3D -> Done`：`ApplicationLoopTest::runsDualCameraEnqueueFlow` 通过。
- [x] legacy 单相机触发和回包匹配：`LegacyCameraWorkflowTest` 覆盖 sequential/counted/ASCII count。
- [x] 相机 payload 写入 `QueueManager` 后，fake robot 出队闭环仍通过 `ApplicationLoopTest::tracksLoopInEventsAndSnapshot`。

**明确不做逐项核对**：
- [x] 未引入 DUCO SDK：grep `DucoCobot|DUCO` 无命中。
- [x] 未引入 Qt Widgets / Modbus：grep `QWidget|QMainWindow|QModbusTcpClient|Modbus` 无命中。
- [x] 未改变 PLC `9999/9090` 解析字段顺序，`PlcProtocolTest` 和 `PlcEndpointTest` 通过。

**关键决策落地**：
- [x] 扩展现有主控：`Application` 组装 `PlcEndpoint`、`CameraEndpoint`、dual/legacy workflow。
- [x] 协议集中在 `protocol`：相机解析只在 `CameraProtocol`。
- [x] 状态机集中在 `core`：dual 在 `EnqueueWorkflow`，legacy 在 `LegacyCameraWorkflow`。
- [x] 配置切换：`camera.flow_mode` 支持 `legacy_single_camera` 和 `dual_camera_11_12`。

**挂载点核对**：
- [x] `config/default.toml`：新增 camera 配置。
- [x] `Application`：PLC 入队分发改为 workflow。
- [x] `CameraEndpoint`：按配置启动 dual server 或 legacy client。
- [x] `EnqueueWorkflow` / `LegacyCameraWorkflow`：入队相机状态机和阶段反馈。
- [x] `CameraProtocol`：2D/3D 截帧和解析。
- [x] 反向 grep 未发现清单外挂载点；拔除沙盘为删除上述挂载点、CMake 源、测试和配置即可移除该 feature。

## 3. 验收场景核对

- [x] S1 dual 监听 2D/3D 且 PLC 保持可用：`CameraEndpointTest::dualModeAccepts2dAnd3dClients` 和 `ApplicationLoopTest::runsDualCameraEnqueueFlow`。
- [x] S2 PLC `11,count=10,pointer=1`：端到端测试确认 3D 收到 `11,10`，队列成对入队。
- [x] S3 2D `READY`：端到端测试确认 2D 收到 `(10)E`；重复 READY 测试在 `EnqueueWorkflowTest::runsDualCameraHappyPath`。
- [x] S4 PLC `12` 且已有 2D 类型：端到端测试确认 3D 收到 `12,10,01,02`。
- [x] S5 无 2D 类型 fallback：`EnqueueWorkflowTest::fallsBackWhen2dResultIsMissing` 确认 `12,11,00,00`。
- [x] S6 3D 双臂帧：端到端测试确认队列 arm1/arm2 source 为 camera，PLC 入队通道收到 `Done`。
- [x] S7 半包 / 非法 segment：`CameraProtocolTest::keeps3dHalfPacket`、`EnqueueWorkflowTest::waitsFor3dHalfPacket`、`rejectsInvalid3dSegments`。
- [x] S8 legacy 单相机：`LegacyCameraWorkflowTest` 覆盖触发、回包写队列、sequential/counted；`CameraEndpointTest::legacyModeDoesNotListenOnDualPorts` 确认不启动 dual 端口。

## 4. 术语一致性

- 相机流程 parity、legacy 单相机、dual camera、2D READY 窗口、3D 双臂帧、入队阶段反馈均在代码命名或日志分类中有一致落点。
- 新增实现概念 `LegacyCameraWorkflow`、`FrameDrainResult` 已回填 design 第 2.1 节。
- 禁用范围词 DUCO / Widgets / Modbus grep 无命中。

## 5. 架构归并

- [x] `.codestable/architecture/ARCHITECTURE.md`：新增相机入队概念、模块索引、落地子系统、关键边界和约束。
- [x] `.codestable/architecture/protocol-camera.md`：新增相机协议、dual 流程、legacy 流程和约束说明。

## 6. requirement 回写

- [x] `.codestable/requirements/camera-flow-parity.md` 已从 `draft` 升级为 `current`。
- [x] 边界补充 dual/legacy 实现能力，变更日志记录 2026-05-24 验收完成。

## 7. roadmap 回写

- [x] `.codestable/roadmap/cpp-spray-control/cpp-spray-control-items.yaml` 中 `camera-flow-parity` 已从 `in-progress` 改为 `done`。
- [x] `.codestable/roadmap/cpp-spray-control/cpp-spray-control-roadmap.md` 子 feature 清单和变更日志已同步。

## 8. attention.md 候选盘点

- [x] 无新增候选。本 feature 继续沿用已记录的 Windows/Qt MinGW ASCII 构建目录约束。

## 9. 遗留

- 后续优化点：真实现场相机联调后，可能需要补充更具体的点位/recipe 字段模型，已在 design 超出范围观察中记录。
- 已知限制：当前仍未接 DUCO SDK、Qt HMI、Modbus；按 roadmap 后续 feature 推进。
- 顺手发现：无。
