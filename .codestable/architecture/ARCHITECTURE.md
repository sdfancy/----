# 喷涂开发 架构总入口

> 状态：当前现状
> 创建日期：2026-05-23

## 1. 项目简介

本仓库用于规划新的 C++ 喷涂控制系统。当前已有 Python 版本队列管理项目作为协议参考，新项目目标是在保留 PLC 与相机现有通讯流程的前提下，用 C++ 重写队列、通讯、机械臂控制和 HMI，并改用新松机械臂二次开发 API 直接控制运动。

## 2. 核心概念 / 术语表

- PLC 入队通道：PLC 连接软件 `9999`，发送入队命令。
- PLC 出队通道：PLC 连接软件 `9090`，发送双机械臂出队指针，软件在同通道回 `1N/1D/2N/2D`。
- Modbus sidecar：独立于 `PlcEndpoint` 的 Smart200 Modbus TCP 旁路客户端，默认禁用。
- AddressTable：`config/modbus_nodes.toml` 中的 Modbus 节点表，保存 name、type、slaveId、address、count、writable、description。
- PlcModbusClient：`io/plc` 下封装 Modbus 连接和读写 API 的客户端；后续 HMI/规则引擎通过它访问 Modbus，不直接访问 Qt SerialBus。
- DiagnosticEvent：diagnostics 层统一事件记录，包含时间、等级、分类、设备、方向、hex payload、count、pointer、错误码和消息。
- RawFrameLog：可配置持久化的 PLC/相机原始通讯 JSONL 日志，不改变 socket 收发。
- DeviceHealth：按设备聚合 online、connectionCount、lastRxAt、lastTxAt、lastErrorAt 和 lastError 的快照。
- DiagnosticsService：Application 内部聚合事件历史、设备健康和文件 sink 的服务，只读暴露给后续 HMI。
- DeviceSimulator：本地联调模拟器，通过公开 TCP 端口复现 PLC 最小闭环和 dual camera 入队，不访问队列私有容器。
- 队列项：由同一 count/pointer 创建的 arm1/arm2 双队列数据。
- 出队缓存：PLC 指针变化后，为某个机械臂准备的待执行数据。
- 相机入队流程：PLC 入队命令触发 legacy 单相机或 dual camera 流程，相机 payload 写入队列项。
- dual camera：软件监听 2D/3D 相机端口，按 `11 -> READY -> 12 -> 3D -> Done` 编排入队。
- legacy 单相机：软件作为 TCP client 连接单相机，按 PLC `1/11/12` 触发并按 sequential/counted 匹配回包。
- DUCO 远程 API：新松机械臂二次开发接口，默认远程端口 `7003`。
- 机械臂控制契约：`robot::IRobotController` 是 `core::DequeueCoordinator` 面向机械臂的唯一接口，fake robot 和 DUCO controller 都走该契约。
- DUCO client role：DUCO 适配层按 motion/control/heartbeat/status 拆分 SDK client 对象，避免多线程阻塞调用共享同一 `DucoCobot`。
- ArmPayload：robot 层从 `(flag,count,...)E` 解析出的机械臂 payload，包含 arm、count、默认空任务和运动数值。
- MotionSegment：robot 层中立运动段，当前覆盖 `movej_pose2`、`movel`、末端 IO 和控制柜 IO。
- MotionRecipe：每个 arm 的最小喷涂任务配置，包含工具/工件坐标、速度、加速度、喷枪 IO 和字段映射。
- FieldConfig：现场配置文件集合，当前包含 `[robot].recipe_path` 指向的 motion recipe 文件。
- MotionRecipeTable：robot 层独立配置表，从 `config/motion_recipes.toml` 加载、校验并按 arm 查询 `MotionRecipe`。
- PoseFieldMapping：recipe 中 `pose_indices` 定义的 6 个 payload 数值索引，规划时映射为 `Pose6d`。
- SprayIoConfig：recipe 中 `spray_io` 和 `spray_io_channel` 定义的喷枪 IO 类型与通道。
- PlannedRobotTask：`core::RobotTask` 加 `MotionSegment` 序列后的 robot 内部执行形态。
- DUCO motion worker：按 arm 串行执行 motion client 阻塞调用的 worker，controller 只做规划和调度粘合。
- FieldRunbook：现场 P0-P6 分阶段验收操作手册，面向人工执行，不自动驱动危险动作。
- FieldChecklist：现场 pass/fail/evidence/sign-off 记录表，用于记录真实 PLC、相机和 DUCO 联调结果。
- EvidenceBundle：一次现场验收的证据目录，包含 commit、配置、recipe、日志、测试输出、checklist 和现场备注。
- DryRunGate：接入真实硬件前必须通过的 fake robot / simulator 离线检查。
- SafetyHoldPoint：机械臂、喷枪或生产观察前必须人工确认的安全停止点。

## 3. 子系统 / 模块索引

- C++ 新项目路线图：`.codestable/roadmap/cpp-spray-control/cpp-spray-control-roadmap.md`
- C++ 文件与函数方案：`.codestable/roadmap/cpp-spray-control/cpp-spray-control-file-function-plan.md`
- 已落地最小闭环：`app -> io/plc -> protocol -> core -> robot/fake -> io/plc feedback`
- 已落地相机入队 parity：`app -> io/plc + io/camera -> core workflow -> QueueManager -> PLC enqueue feedback`
- 已落地 DUCO SDK 适配骨架：`app -> robot::IRobotController -> robot/duco role clients`
- 已落地 DUCO 任务执行基础：`QueueManager raw payload -> MotionPlanner -> DucoMotionWorker -> DUCO motion client -> XN/XD`
- 已落地 Modbus 旁路预留：`config -> ModbusAddressTable -> PlcModbusClient -> optional Qt SerialBus transport`
- 已落地诊断日志基础：`Application signals -> DiagnosticsService -> EventLog + DeviceHealthRegistry + RawFrameFileSink`
- 已落地现场 recipe 配置：`config -> MotionRecipeTable -> DucoRobotController::setMotionRecipe -> MotionPlanner`
- 已落地硬件验收包：`docs/runbook + docs/checklist + tools/field_acceptance/collect_evidence.ps1`
- 相机协议说明：`.codestable/architecture/protocol-camera.md`

### 3.1 已落地最小闭环

- `app`：`Application` 负责加载配置、组装 `PlcEndpoint`、`QueueManager`、`FakeRobotController` 和 `DequeueCoordinator`，`spray_control --headless --simulate-robot` 可启动无界面主控。
- `protocol`：`PlcProtocol` 按大端解析 PLC 入队 6 字节 `command,count,pointer`，解析 PLC 出队 4 字节 `arm1Pointer,arm2Pointer`，并生成 `1N/1D/2N/2D`。
- `core`：`QueueManager` 维护 arm1/arm2 成对队列、出队指针变化、出队缓存、默认 payload 和队列 snapshot；重复出队指针不重复触发任务。
- `io/plc`：`PlcEndpoint` 使用 Qt Network 监听 `9999/9090`，记录 raw frame，并通过 `9090` 当前连接发送出队反馈。
- `robot/fake`：`FakeRobotController` 按 arm 串行模拟 accepted/finished；`DequeueCoordinator` 把出队缓存转成任务，并在 accepted/finished 时回 `XN/XD`。

### 3.2 已落地相机入队流程

- `protocol`：`CameraProtocol` 负责 2D `READY` / CSV result 解析、3D `(...)EA(...)E` 截帧、半包保留和 arm segment 分流。
- `io/camera`：`CameraEndpoint` 按配置启动 dual 2D/3D TCP server，或 legacy 单相机 TCP client；socket 回调只读写 bytes 并发信号。
- `core`：`EnqueueWorkflow` 承载 dual camera `11/12/READY/Done` 状态机；`LegacyCameraWorkflow` 承载旧单相机触发、sequential/counted 匹配和 `1Done/2Done`。
- `app`：`Application` 根据 `camera.flow_mode` 组装 dual 或 legacy workflow，并把 PLC 入队帧、相机 payload、入队阶段反馈接起来。
- `io/plc`：`PlcEndpoint` 保持 `9999/9090` 双 TCP 通道，并新增在 `9999` 当前连接发送入队阶段反馈的能力。

### 3.3 已落地 DUCO SDK 适配骨架

- `robot`：`IRobotController` 定义连接、prepare、出队任务、stop/pause/resume/readStatus，以及 accepted/finished/status/warning 信号。
- `robot/fake`：`FakeRobotController` 实现 `IRobotController`，继续用于无真实 SDK 环境和 PLC 出队闭环测试。
- `robot/duco`：`DucoRobotController` 管理 DUCO role clients；prepare 顺序为 open role clients -> heartbeat -> power_on(true) -> enable(true) -> readStatus。
- `robot/duco`：当前默认构建不链接现场 SDK，`SPRAY_ENABLE_DUCO` 和 SDK include/lib 作为可选构建入口；缺 SDK 时 fake 构建和测试不受影响。
- `app`：`Application` 按 `[robot].mode` 选择 fake 或 DUCO controller，并在 start/stop 中接入 robot lifecycle。

### 3.4 已落地 DUCO 任务执行基础

- `robot`：`MotionPlanner` 解析 raw payload，校验 arm/count/数值字段，并按 `MotionRecipe` 生成 `movej_pose2 -> IO on -> movel -> IO off` 的中立段序列。
- `robot`：`MotionTypes` 定义 `ArmPayload`、`Pose6d`、`Joint6d`、`MotionSegment`、`MotionRecipe` 和 `PlannedRobotTask`，不把 DUCO SDK 类型暴露给 core。
- `robot/duco`：`IDucoClient` seam 暴露 `moveJPose2`、`moveL`、`setToolDigitalOut`、`setStandardDigitalOut`，默认 unavailable client 返回失败但不要求现场 SDK。
- `robot/duco`：`DucoRobotController` 对默认空任务直接 accepted/finished；对非默认任务先规划再按 arm 投递执行，规划失败或 recipe 缺失只 warning 和失败结束。
- `robot/duco`：`DucoMotionWorker` 只使用 motion client 执行阻塞 motion/IO；任一段返回 `-1` 时尝试关闭已打开喷枪 IO，并让 controller 进入 faulted。
- `core`：`DequeueCoordinator` 只在 `taskFinished(ok=true)` 时 mark done 并发 `XD`；`ok=false` 不发送正常完成反馈。

### 3.5 已落地 Modbus 旁路预留

- `config`：`ModbusConfig` 表达 enabled、host、port、defaultSlaveId、timeoutMs、retries 和 addressTablePath；默认 `enabled=false`。
- `io/plc`：`ModbusAddressTable` 加载和校验 `config/modbus_nodes.toml`，允许空表，拒绝重复 name、非法 slave/address/count 和非单点 coil。
- `io/plc`：`PlcModbusClient` 提供 `connectToPlc`、`disconnectFromPlc`、`readHoldingRegisters`、`writeHoldingRegisters`、`writeCoil`、`readNode`、`writeNode`。
- `io/plc`：`SPRAY_ENABLE_MODBUS=OFF` 时不要求 Qt SerialBus；启用后才链接 `Qt6::SerialBus` 并使用 `QModbusTcpClient`。
- `io/plc`：禁用、未连接、非法地址、只读节点、协议异常和超时都返回 `ModbusError`；客户端发 `warning` 信号供 diagnostics/HMI 记录。
- `io/plc`：Modbus 不挂入 `PlcEndpoint`，不参与 PLC `9999/9090` 入队、出队、反馈链路。

### 3.6 已落地诊断日志基础

- `diagnostics`：`DiagnosticEvent`/`EventFilter`/`EventLog` 提供内存事件历史，支持按 level、category、device、count、pointer 和时间窗口查询，并用 bounded ring buffer 暴露 dropped 计数。
- `diagnostics`：`DiagnosticsService` 接收 PLC、相机、robot、workflow 的 rawFrame、warning、status 和业务事件，更新 `EventLog` 与 `DeviceHealthRegistry`。
- `diagnostics`：`RawFrameFileSink` 按 `[logging]` 配置写 `raw-frames.jsonl` 与 `events.jsonl`，写入失败降级为 diagnostics warning，不阻断主控流程。
- `diagnostics`：`DeviceSimulator` 通过公开 TCP 端口复现 PLC enqueue/dequeue 和 dual camera `11 -> READY -> 12 -> 3D -> Done`，用于本地联调和集成测试。
- `app`：`Application` 暴露 `events(filter)`、`deviceHealthSnapshot()` 和 `flushDiagnostics()` 只读入口，后续 HMI 消费快照，不直接读取设备对象或队列私有容器。

### 3.7 已落地现场 recipe 配置

- `config`：`RobotConfig::recipePath` 默认指向 `config/motion_recipes.toml`，`AppConfig` 只保存路径，不解析 recipe 数组。
- `robot`：`MotionRecipeTable` 加载 TOML-like `[[recipes]]`，校验 arm1/arm2、重复 arm、字段映射、速度、加速度和喷枪 IO。
- `config`：`config/motion_recipes.toml` 只提供 arm1/arm2 示例 recipe，真实现场字段、速度、坐标系和 IO 必须现场替换。
- `app`：DUCO 模式初始化时加载 recipe table，并把每条 recipe 注入 `DucoRobotController::setMotionRecipe()`；加载失败直接让 initialize 失败。
- `robot/duco`：非默认任务仍由 `MotionPlanner` 消费 `MotionRecipe` 规划，缺失或 disabled recipe 只 warning 和 `taskFinished(false)`，不发送正常 `XD`。
- `robot/fake`：fake robot 和默认空任务不依赖 recipe 文件。

### 3.8 已落地硬件验收包

- `docs/hardware_acceptance_runbook.md`：按 P0 准备、P1 dry-run、P2 PLC、P3 相机、P4 DUCO prepare、P5 低速运动、P6 生产观察组织现场验收。
- `docs/hardware_acceptance_checklist.md`：记录每阶段 pass/fail、时间、操作者、证据、SafetyHoldPoint 和签字。
- `tools/field_acceptance/collect_evidence.ps1`：只复制配置、recipe、日志、test-output、checklist 并生成 summary，不连接 PLC、相机、DUCO 或 Modbus。
- 真实现场通过/失败结果不写入默认仓库状态，由现场 EvidenceBundle、checklist 或后续 issue 记录。

## 4. 关键架构决定

- 当前阶段不修改 PLC 与相机外部通讯流程。
- 相机入队状态机不放入 `QueueManager`；`QueueManager` 只负责成对队列、缓存和 `storeCameraData()`。
- 机械臂侧不再依赖示教器程序的 `data/1` TCP 协议，改为软件通过 DUCO 远程 API 主动执行喷涂任务。
- `core::DequeueCoordinator` 只依赖 `robot::IRobotController`，不得直接依赖 fake 或 DUCO SDK 类型。
- `core::RobotTask` 保留 raw payload，payload 解析、运动规划、recipe 校验和 DUCO motion 调用都在 robot 层完成。
- 现场 recipe 参数必须来自配置文件或测试注入，不得把真实字段映射、速度、坐标系或喷枪 IO 硬编码到 C++ 代码。
- `taskFinished(ok=false)` 代表任务失败或安全拒绝，不得发送正常 `XD`；当前不新增 PLC 错误反馈码。
- HMI 建议使用 Qt Widgets；通讯层使用 Qt Network，预留 Modbus 使用 Qt SerialBus。
- Modbus 是默认禁用的旁路能力；Smart200 地址只能来自地址表配置，不能写入业务代码。
- 硬件验收 helper 只能做资料收集和离线证据归档，不得自动执行真实 motion、IO 或 Modbus 写入。

## 5. 已知约束 / 硬边界

- 单个 Markdown 文档不超过 300 行，超过必须拆分。
- 队列语义以旧 Python 项目中已稳定的 PLC、相机、队列状态机为参考。
- PLC 与相机 socket 回调不得阻塞等待对方返回；跨设备编排放在 core workflow。
- dual camera `Done` 对同一 count 只发送一次，且必须 arm1/arm2 的 3D payload 都写入队列。
- DUCO API 多线程调用必须隔离对象：阻塞运动、任务控制、心跳不得共享同一个 `DucoCobot` 对象。
- DUCO `open()` 未成功前禁止上电、使能、任务控制和运动；任一 DUCO 调用返回 `-1` 进入 faulted 并发 warning，不伪造成任务完成。
- 非默认 DUCO 任务没有有效 `MotionRecipe` 时必须拒绝执行；现场 recipe 文件必须同时配置 arm1/arm2，真实字段映射和喷枪接线由现场替换示例参数。
- `PlcEndpoint` 不 include 或持有 `PlcModbusClient`；Modbus 写入不得触发队列、相机流程或 DUCO motion。
- diagnostics 只采集事件、健康状态和日志，不得调用 `QueueManager` 写接口或触发 DUCO motion。
- socket 回调不得直接做阻塞文件写入；diagnostics 文件持久化必须走队列和 flush。
- 现场真实 IP、坐标、相机字段、喷枪 IO、Smart200 地址和凭证不得提交到仓库。
- 当前已接入 PLC、相机入队流程、DUCO 适配骨架、DUCO 任务执行基础、Modbus 旁路预留、诊断日志基础、现场 recipe 文件化配置和硬件验收包；仍不包含现场真实通过记录。
- Windows/Qt MinGW 构建在中文源码路径下不能把构建目录放在项目内，Qt `moc` 会失败；使用 ASCII 构建目录。
