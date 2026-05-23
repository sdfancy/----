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
- DUCO 远程 API：新松机械臂二次开发接口，默认远程端口 `7003`。

## 3. 子系统 / 模块索引

- C++ 新项目路线图：`.codestable/roadmap/cpp-spray-control/cpp-spray-control-roadmap.md`
- C++ 文件与函数方案：`.codestable/roadmap/cpp-spray-control/cpp-spray-control-file-function-plan.md`
- 已落地最小闭环：`app -> io/plc -> protocol -> core -> robot/fake -> io/plc feedback`

### 3.1 已落地最小闭环

- `app`：`Application` 负责加载配置、组装 `PlcEndpoint`、`QueueManager`、`FakeRobotController` 和 `DequeueCoordinator`，`spray_control --headless --simulate-robot` 可启动无界面主控。
- `protocol`：`PlcProtocol` 按大端解析 PLC 入队 6 字节 `command,count,pointer`，解析 PLC 出队 4 字节 `arm1Pointer,arm2Pointer`，并生成 `1N/1D/2N/2D`。
- `core`：`QueueManager` 维护 arm1/arm2 成对队列、出队指针变化、出队缓存、默认 payload 和队列 snapshot；重复出队指针不重复触发任务。
- `io/plc`：`PlcEndpoint` 使用 Qt Network 监听 `9999/9090`，记录 raw frame，并通过 `9090` 当前连接发送出队反馈。
- `robot/fake`：`FakeRobotController` 按 arm 串行模拟 accepted/finished；`DequeueCoordinator` 把出队缓存转成任务，并在 accepted/finished 时回 `XN/XD`。

## 4. 关键架构决定

- 当前阶段不修改 PLC 与相机外部通讯流程。
- 机械臂侧不再依赖示教器程序的 `data/1` TCP 协议，改为软件通过 DUCO 远程 API 主动执行喷涂任务。
- HMI 建议使用 Qt Widgets；通讯层使用 Qt Network，预留 Modbus 使用 Qt SerialBus。

## 5. 已知约束 / 硬边界

- 单个 Markdown 文档不超过 300 行，超过必须拆分。
- 队列语义以旧 Python 项目中已稳定的 PLC、相机、队列状态机为参考。
- DUCO API 多线程调用必须隔离对象：阻塞运动、任务控制、心跳不得共享同一个 `DucoCobot` 对象。
- 当前最小闭环不包含 DUCO SDK、Qt Widgets、相机 `9001/9002` 和 Modbus；这些能力由后续 roadmap item 单独接入。
- Windows/Qt MinGW 构建在中文源码路径下不能把构建目录放在项目内，Qt `moc` 会失败；使用 ASCII 构建目录。
