# 喷涂开发 架构总入口

> 状态：当前现状
> 创建日期：2026-05-23

## 1. 项目简介

本仓库用于规划新的 C++ 喷涂控制系统。当前已有 Python 版本队列管理项目作为协议参考，新项目目标是在保留 PLC 与相机现有通讯流程的前提下，用 C++ 重写队列、通讯、机械臂控制和 HMI，并改用新松机械臂二次开发 API 直接控制运动。

## 2. 核心概念 / 术语表

- PLC 入队通道：PLC 连接软件 `9999`，发送入队命令。
- PLC 出队通道：PLC 连接软件 `9090`，发送双机械臂出队指针，软件在同通道回 `1N/1D/2N/2D`。
- 队列项：由同一 count/pointer 创建的 arm1/arm2 双队列数据。
- 出队缓存：PLC 指针变化后，为某个机械臂准备的待执行数据。
- 相机入队流程：PLC 入队命令触发 legacy 单相机或 dual camera 流程，相机 payload 写入队列项。
- dual camera：软件监听 2D/3D 相机端口，按 `11 -> READY -> 12 -> 3D -> Done` 编排入队。
- legacy 单相机：软件作为 TCP client 连接单相机，按 PLC `1/11/12` 触发并按 sequential/counted 匹配回包。
- DUCO 远程 API：新松机械臂二次开发接口，默认远程端口 `7003`。

## 3. 子系统 / 模块索引

- C++ 新项目路线图：`.codestable/roadmap/cpp-spray-control/cpp-spray-control-roadmap.md`
- C++ 文件与函数方案：`.codestable/roadmap/cpp-spray-control/cpp-spray-control-file-function-plan.md`
- 已落地最小闭环：`app -> io/plc -> protocol -> core -> robot/fake -> io/plc feedback`
- 已落地相机入队 parity：`app -> io/plc + io/camera -> core workflow -> QueueManager -> PLC enqueue feedback`
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

## 4. 关键架构决定

- 当前阶段不修改 PLC 与相机外部通讯流程。
- 相机入队状态机不放入 `QueueManager`；`QueueManager` 只负责成对队列、缓存和 `storeCameraData()`。
- 机械臂侧不再依赖示教器程序的 `data/1` TCP 协议，改为软件通过 DUCO 远程 API 主动执行喷涂任务。
- HMI 建议使用 Qt Widgets；通讯层使用 Qt Network，预留 Modbus 使用 Qt SerialBus。

## 5. 已知约束 / 硬边界

- 单个 Markdown 文档不超过 300 行，超过必须拆分。
- 队列语义以旧 Python 项目中已稳定的 PLC、相机、队列状态机为参考。
- PLC 与相机 socket 回调不得阻塞等待对方返回；跨设备编排放在 core workflow。
- dual camera `Done` 对同一 count 只发送一次，且必须 arm1/arm2 的 3D payload 都写入队列。
- DUCO API 多线程调用必须隔离对象：阻塞运动、任务控制、心跳不得共享同一个 `DucoCobot` 对象。
- 当前已接入 PLC 和相机入队流程；仍不包含 DUCO SDK、Qt Widgets 和 Modbus，这些能力由后续 roadmap item 单独接入。
- Windows/Qt MinGW 构建在中文源码路径下不能把构建目录放在项目内，Qt `moc` 会失败；使用 ASCII 构建目录。
