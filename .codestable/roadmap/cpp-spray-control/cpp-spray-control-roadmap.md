---
doc_type: roadmap
slug: cpp-spray-control
status: active
created: 2026-05-23
last_reviewed: 2026-05-24
tags: [cpp, qt, plc, duco, spray, modbus]
related_requirements: []
related_architecture: []
---

# C++ 喷涂控制新项目

## 1. 背景

现有队列管理使用 Python 实现，PLC、相机、机械臂之间的队列协议已经基本清楚，但现场问题集中在两点：一是 Python 对 PLC 高频 TCP 长连接、日志和多线程调度的现场稳定性不足；二是机械臂通过示教器程序运行，兼容性和通讯可靠性差，经常出现机械臂不执行或通讯异常。

新项目使用 C++ 重写主控软件，保留 PLC 与相机的现有通讯流程，队列逻辑参考旧 Python 项目；机械臂侧改为通过新松 DUCO 远程控制 API 直接执行喷涂动作。项目框架建议为 C++20 + CMake + Qt 6：Qt Widgets 做 HMI，Qt Network 做 TCP 通讯，Qt SerialBus 预留 Modbus TCP。

## 2. 范围与明确不做

### 本 roadmap 覆盖

- C++ 项目框架、目录、模块边界和每个建议文件/函数职责。
- PLC 双 TCP 通道、相机接入、队列与出队缓存的 C++ 设计。
- 新松机械臂 DUCO API 适配层、任务执行器和 PLC `XN/XD` 反馈语义。
- Qt HMI 的页面结构、数据模型和交互职责。
- Siemens Smart200 后续 Modbus TCP 读写能力预留。

### 明确不做

- 不修改 PLC 与软件之间 `9999/9090` 的通讯流程和报文字段。
- 不修改相机触发、2D/3D 接入和相机回包的业务流程。
- 本次不生成 Qt 界面代码，界面只做设计方案。
- 第一阶段不引入 ROS2/MoveIt；固定喷涂路径和现场节拍优先用 DUCO 远程 API。
- 不把旧 Python 项目嵌入新项目；旧项目只作为协议和行为参考。

## 3. 模块拆分（概设）

```
C++ 喷涂控制系统
├── app：进程入口、生命周期、配置加载
├── protocol：PLC/相机/队列载荷解析与编码
├── core：队列状态机、入队流程、出队调度
├── io：TCP server/client、PLC 端点、相机端点、Modbus 客户端
├── robot：DUCO SDK 封装、喷涂任务执行、仿真机器人
├── ui：Qt Widgets HMI、表格模型、页面与操作命令
└── diagnostics：日志、健康状态、快照、错误码和测试模拟器
```

### app · 进程与配置

- **职责**：加载 TOML/JSON 配置，创建各服务，控制 start/stop，支持 `--headless` 无界面运行。
- **承载的子 feature**：`cpp-minimal-loop`, `qt-hmi-shell`, `field-config-and-recipes`
- **触碰的现有代码 / 模块**：全新 C++ 项目，参考旧 `src/main.py` 的模块编排。

### protocol · 报文协议

- **职责**：把原始 bytes 转成强类型结构，集中处理大小端、截帧、默认值、相机 payload 解析。
- **承载的子 feature**：`cpp-minimal-loop`, `camera-flow-parity`
- **触碰的现有代码 / 模块**：参考旧 `src/utils/data_parser.py`、`queue_manager.py` 中的数据格式逻辑。

### core · 队列和流程状态机

- **职责**：维护 arm1/arm2 双队列、出队缓存、预取、默认值、晚到相机数据丢弃、`XN/XD` 触发时机。
- **承载的子 feature**：`cpp-minimal-loop`, `camera-flow-parity`, `duco-task-executor`
- **触碰的现有代码 / 模块**：参考旧 `EnhancedQueueManager` 和 `EnqueueWorkflowCoordinator`。

### io · 设备通讯

- **职责**：PLC `9999/9090` TCP 服务、2D/3D 相机 TCP 服务、未来 Smart200 Modbus TCP 客户端。
- **承载的子 feature**：`cpp-minimal-loop`, `camera-flow-parity`, `modbus-plc-reservation`
- **触碰的现有代码 / 模块**：参考旧 `plc_handler.py`、`camera_server.py`。

### robot · 新松机械臂控制

- **职责**：封装 DUCO `DucoCobot`，按 arm_id 执行喷涂任务，管理心跳、上电使能、暂停恢复、停止、异常回收。
- **承载的子 feature**：`duco-sdk-adapter`, `duco-task-executor`, `hardware-acceptance`
- **触碰的现有代码 / 模块**：替换旧 `arm_controller.py` 的 TCP `data/1` 协议，不保留示教器依赖。

### ui · Qt HMI

- **职责**：展示系统总览、队列、缓存、PLC/相机/机械臂连接、运行日志、配置和手动控制。
- **承载的子 feature**：`qt-hmi-shell`
- **触碰的现有代码 / 模块**：参考旧 HMI snapshot，不复用网页实现。

### diagnostics · 日志与验证

- **职责**：原始通讯日志、事件历史、健康检查、设备模拟器、单元测试和现场验收脚本。
- **承载的子 feature**：`diagnostics-and-logs`, `hardware-acceptance`
- **触碰的现有代码 / 模块**：参考旧 `tools/device_simulator.py` 和测试用例。

## 4. 模块间接口契约 / 共享协议（架构层详设）

### 4.1 PLC 入队协议

**方向**：PLC -> `io::PlcEndpoint` -> `core::EnqueueWorkflow`
**形式**：TCP `9999`，固定 6 字节，大端序。

```
struct PlcEnqueueFrame { uint16_t command; uint16_t count; uint16_t pointer; QByteArray extra; };
parsePlcEnqueue(QByteArray raw) -> expected<PlcEnqueueFrame, ProtocolError>
```

**约束**：
- `command=0x0001/0x000B/0x000C` 分别表示 `01/11/12`。
- 字段顺序默认 `command,count,pointer`，保留配置切换 `command,pointer,count`。
- 软件收到合法帧后必须非阻塞处理，不允许在 socket 回调线程执行机器人阻塞运动。

### 4.2 PLC 出队与反馈协议

**方向**：PLC -> `core::QueueManager`; `core::QueueManager` -> PLC
**形式**：TCP `9090` 双向。

```
struct PlcDequeueFrame { uint16_t arm1Pointer; uint16_t arm2Pointer; };
parsePlcDequeue(QByteArray raw) -> expected<PlcDequeueFrame, ProtocolError>
buildPlcFeedback(int armId, FeedbackStage stage) -> QByteArray  // "1N", "1D", "2N", "2D"
```

**约束**：
- 出队帧必须为 4 字节。
- 只有某个 arm 的 pointer 变化时，才更新该 arm 的出队缓存。
- 新项目中 `XN` 表示对应机械臂喷涂任务已被软件接受并开始执行；`XD` 表示 DUCO 任务正常结束或默认空任务已安全收尾。

### 4.3 相机协议

**方向**：`core::EnqueueWorkflow` <-> `io::CameraEndpoint`
**形式**：TCP，2D 默认 `9001`，3D 默认 `9002`。

```
sendCameraTrigger(cameraKey, QByteArray payload) -> bool
parseCameraPayload(cameraKey, QByteArray raw) -> vector<CameraMessage>
storeCameraData(int armId, uint16_t count, uint16_t pointer, QByteArray payload) -> bool
```

**约束**：
- legacy 单相机模式保留 `01/11/12` 触发与 `1Done/2Done` 阶段反馈。
- dual camera 模式保留 `11,count`、2D `READY`、`12,count,type1,type2`、3D `(...)EA(...)E` 流程。
- 相机晚到数据如果对应队列项已超时或已出队，按配置丢弃。

### 4.4 队列共享状态

```
struct ArmQueueItem {
  int armId; uint16_t pointer; uint16_t count; uint64_t enqueueSeq;
  QByteArray payload; QueueItemStatus status; PayloadSource source;
  bool dequeued; bool confirmed; int sendCount; QDateTime createdAt; QDateTime updatedAt;
};
struct DequeueCache { int armId; uint16_t triggerPointer; int prefetchOffset; ArmQueueItem item; };
```

**约束**：
- arm1/arm2 必须同一 pointer 成对入队，溢出按最老 `enqueueSeq` 成对删除。
- `prefetch_offset=1` 时选择目标项后一拍数据；首件暖机可配置为空任务。
- 默认值仍采用 `(1000,count)E`、`(2000,count)E`、`(1000,0)E`、`(2000,0)E` 语义。

### 4.5 机器人控制接口

**方向**：`core::DequeueCoordinator` -> `robot::IRobotController`
**形式**：异步命令队列，机器人阻塞调用只在 robot worker 线程执行。

```
struct RobotTask { int armId; uint16_t count; uint16_t pointer; vector<MotionSegment> segments; bool defaultNoop; };
class IRobotController {
  connect(), disconnect(), prepare(), enqueueTask(RobotTask), stop(), pause(), resume(), readStatus();
}
```

**约束**：
- 每个 arm 至少一个 DUCO command worker；心跳、任务控制、运动阻塞调用使用不同 `DucoCobot` 对象。
- DUCO 连接默认 `ip:7003`，`open()` 成功后才允许上电、使能和运动。
- 运动命令优先使用 `movej_pose2` 进近、`movel` 喷涂路径、`set_tool_digital_out` 或 `set_standard_digital_out` 控制喷枪。

### 4.6 Modbus 预留接口

**方向**：UI/规则引擎 -> `io::PlcModbusClient` -> Smart200
**形式**：Modbus TCP，默认禁用。

```
readHoldingRegisters(slaveId, start, count) -> expected<vector<uint16_t>, ModbusError>
writeHoldingRegisters(slaveId, start, vector<uint16_t>) -> expected<void, ModbusError>
writeCoil(slaveId, address, bool value) -> expected<void, ModbusError>
```

**约束**：
- Modbus 不参与第一阶段 `9999/9090` 主流程，避免影响现有 PLC 程序。
- 地址表必须放在配置文件，禁止把 Smart200 地址硬编码进业务代码。

### 4.7 HMI 快照接口

**方向**：core/io/robot -> `diagnostics::SnapshotService` -> UI model
**形式**：内存快照。

```
SystemSnapshot buildSnapshot()
QueueSnapshot queueSnapshot()
DeviceSnapshot deviceSnapshot()
RobotSnapshot robotSnapshot(int armId)
```

**约束**：
- UI 只能读快照和发送命令，不直接改队列内部容器。
- 表格模型刷新频率默认 500-1000 ms；原始通讯日志不刷入控制台，只写文件并在 UI 分页查看。

## 5. 子 feature 清单

1. **cpp-minimal-loop**（done，feature `2026-05-23-cpp-minimal-loop`）— CMake/Qt 项目骨架、PLC 双端口、队列核心、fake robot 执行器，跑通指针到 `XN/XD` 的最小闭环。
2. **camera-flow-parity**（done，feature `2026-05-23-camera-flow-parity`）— 复刻 legacy 单相机和 dual camera 入队流程，保证相机数据入队与默认值策略一致。
3. **duco-sdk-adapter**（done，feature `2026-05-24-duco-sdk-adapter`）— 封装 DUCO C++ 远程 API、连接、心跳、上电使能、状态读取和任务控制。
4. **duco-task-executor**（done，feature `2026-05-24-duco-task-executor`）— 把队列 payload 转成喷涂任务并调用 DUCO 运动接口执行。
5. **qt-hmi-shell**（done，feature `2026-05-24-qt-hmi-shell`）— Qt Widgets HMI 页面、表格模型、设备状态和操作命令设计方案。
6. **modbus-plc-reservation** — Smart200 Modbus TCP 客户端、地址表配置和读写 API 预留。
7. **diagnostics-and-logs** — 原始通讯日志、事件历史、健康检查、错误码和测试模拟器。
8. **field-config-and-recipes** — 现场配置、机械臂 recipe、点位字段映射、喷枪 IO 和速度参数。
9. **hardware-acceptance** — 真实 PLC、相机、新松机械臂联调验收脚本和现场 checklist。

**最小闭环**：第 1 条 `cpp-minimal-loop` 做完后，可以不用真实机械臂，使用 fake robot 验证 PLC 出队指针变化 -> 准备缓存 -> fake 执行 -> PLC 收到 `1N/1D/2N/2D`。

## 6. 排期思路

先做 `cpp-minimal-loop`，因为它验证 C++ 通讯线程、队列锁边界、PLC 反馈语义是否成立。随后补相机流程 parity，确保新项目没有改变 PLC/相机外部流程。DUCO 适配和任务执行分两步做：先证明 SDK 连接与状态控制，再让队列 payload 驱动真实运动。Qt HMI 和 Modbus 可以并行推进，但不能阻塞主控闭环。

## 7. 观察项

- 需要确认新松 DUCO SDK 的 `include/lib/x64` 实际文件名、授权方式和现场控制器 Core 版本。
- 需要确认相机 payload 中点位字段的真实含义：坐标单位、姿态字段、喷枪开关点、路径段分隔规则。
- 需要确认 PLC 是否仍会给机械臂发 IO 启动信号；新方案中机械臂执行由软件直接控制，该 IO 应停用或变为无害监控。
- 需要提供 Smart200 Modbus 地址表，才能把预留接口变成具体读写项。
- 需要确认喷枪 IO 接在机械臂末端 IO、控制柜 IO，还是 PLC 输出。

## 8. 变更日志

- 2026-05-23：创建 C++ 喷涂控制新项目 roadmap 初稿。
- 2026-05-23：完成 `cpp-minimal-loop`，最小闭环进入 done。
- 2026-05-24：完成 `camera-flow-parity`，相机入队流程进入 done。
- 2026-05-24：完成 `duco-sdk-adapter`，机械臂控制契约和 DUCO SDK 适配骨架进入 done。
- 2026-05-24：完成 `duco-task-executor`，队列 payload 到 DUCO 运动段规划和执行基础进入 done。
- 2026-05-24：完成 `qt-hmi-shell` design-only 方案，Qt HMI 页面、快照模型和命令边界进入 done。
