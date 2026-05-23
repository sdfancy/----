# C++ 喷涂控制项目文件与函数方案

本文件是 `cpp-spray-control-roadmap.md` 的文件级补充。函数名是建议契约，后续实现可按 C++ 命名规范微调，但职责不要跨模块漂移。

## 1. 框架选择

- **推荐**：C++20 + CMake + Qt 6 Widgets/Network/SerialBus + MSVC x64。
- **原因**：Qt Widgets 适合工业 HMI；Qt Network 提供 `QTcpServer/QTcpSocket`；Qt SerialBus 提供 `QModbusTcpClient`；DUCO 文档示例偏 Visual Studio/x64。
- **边界**：核心逻辑不直接依赖 UI 控件；HMI 通过 snapshot/model 读状态，通过 command service 发操作。
- **不优先用 ROS2**：当前目标是稳定替代示教器程序并保持 PLC/相机流程，ROS2/MoveIt 依赖重，适合未来复杂规划，不适合作第一阶段主控。

## 2. 构建与配置

- `CMakeLists.txt`：定义 `spray_control`、`spray_tests`；查找 Qt `Core Widgets Network SerialBus Test`；链接 DUCO SDK。
- `cmake/FindDucoRemoteApi.cmake`：`find_duco_remote_api()` 查找 `include/`、`lib/`、运行时 DLL。
- `cmake/ProjectOptions.cmake`：`enable_project_warnings()`、`enable_sanitizers_if_supported()`、`set_output_dirs()`。
- `config/default.toml`：PLC、camera、robot、queue、logging、hmi 默认配置。
- `config/recipes/arm1.toml` / `arm2.toml`：喷涂路径字段映射、速度、加速度、工具/工件坐标系、喷枪 IO。
- `config/modbus_nodes.toml`：Smart200 Modbus 节点地址表；默认空表且 disabled。

## 3. app

- `src/app/main.cpp`
  - `main(int argc, char** argv)`：创建 `QApplication` 或 `QCoreApplication`，启动 `Application`。
  - `parseCommandLine()`：解析 `--config`、`--headless`、`--simulate-robot`。
  - `installQtMessageHandler()`：把 Qt 日志接入 diagnostics 日志系统。
- `src/app/Application.h/.cpp`
  - `Application::initialize()`：加载配置，创建 service，连事件总线。
  - `Application::start()`：启动 PLC、camera、robot、modbus、HMI。
  - `Application::stop()`：按 robot -> camera -> PLC -> logging 顺序关闭。
  - `Application::snapshot()`：返回 `SystemSnapshot` 给 UI。
  - `Application::handleFatalError()`：统一进入安全停止流程。
- `src/app/AppContext.h`
  - `AppContext::config()`：访问只读配置。
  - `AppContext::eventBus()`：访问事件总线。
  - `AppContext::services()`：集中持有核心服务指针。

## 4. config

- `src/config/AppConfig.h/.cpp`
  - `AppConfig::load(path)`：读取 TOML/JSON。
  - `AppConfig::validate()`：校验端口、队列长度、DUCO IP、recipe 路径。
  - `AppConfig::plc()` / `camera()` / `robot()` / `queue()` / `hmi()`：返回分区配置。
- `src/config/RecipeRepository.h/.cpp`
  - `loadAll(directory)`：加载每个 arm 的 recipe。
  - `recipeForArm(int armId)`：查询机械臂 recipe。
  - `validateRecipe(SprayRecipe)`：检查字段映射、速度、IO 范围。

## 5. domain

- `src/domain/Types.h`
  - 定义 `ArmId`、`QueueItemStatus`、`PayloadSource`、`FeedbackStage`、`DeviceState`、`TaskStateCode`。
- `src/domain/Payloads.h/.cpp`
  - `parseArmPayload(QByteArray)`：解析 `(flag,count,points...)E`。
  - `formatDefaultPayload(armId, count)`：生成 `(1000,count)E` / `(2000,count)E`。
  - `isDefaultPayload(ArmPayload)`：识别默认/空任务。
- `src/domain/MotionTypes.h`
  - 定义 `Pose6d`、`Joint6d`、`MotionSegment`、`RobotTask`、`RobotStatus`。
- `src/domain/Snapshots.h`
  - 定义 `QueueSnapshot`、`DeviceSnapshot`、`RobotSnapshot`、`SystemSnapshot`。

## 6. protocol

- `src/protocol/ByteCodec.h/.cpp`
  - `readU16BE(QByteArray, offset)`：读取大端 16 位整数。
  - `writeU16BE(uint16_t)`：写大端 16 位整数。
  - `toHex(QByteArray)`：日志用十六进制文本。
- `src/protocol/PlcProtocol.h/.cpp`
  - `parseEnqueueFrame(QByteArray)`：解析 6 字节入队帧。
  - `parseDequeueFrame(QByteArray)`：解析 4 字节出队指针。
  - `buildFeedback(int armId, FeedbackStage)`：生成 `1N/1D/2N/2D`。
  - `describeFrame(channel, raw)`：给 HMI 和日志展示报文含义。
- `src/protocol/CameraProtocol.h/.cpp`
  - `parse2dFrame(QByteArray)`：解析 READY、`count,type1,type2`。
  - `split3dFrames(QByteArray&)`：按 `(...)EA(...)E` 从缓冲区取完整帧。
  - `parse3dSegment(QString)`：取 `armId/count/payload`。
  - `extractArmId(QByteArray)`：按 flag 千位判断 arm1/arm2。

## 7. core

- `src/core/QueueManager.h/.cpp`
  - `enqueuePair(count, pointer)`：arm1/arm2 成对建队列项。
  - `handleLegacyPlcCommand(command, count, pointer)`：处理 `01/11/12` 单相机流程上下文。
  - `storeCameraData(armId, count, pointer, payload)`：写入相机数据。
  - `handleDequeuePointers(PlcDequeueFrame)`：比较指针变化并准备缓存。
  - `prepareCacheLocked(armId, pointer)`：创建/覆盖出队缓存。
  - `selectPrefetchItem(armId, target)`：实现 `prefetch_offset`。
  - `markTaskAccepted(armId, pointer)`：任务开始后触发 `XN`。
  - `markTaskDone(armId, count)`：任务完成后触发 `XD`。
  - `snapshot()`：生成队列和缓存快照。
- `src/core/EnqueueWorkflow.h/.cpp`
  - `handlePlcFrame(PlcEnqueueFrame)`：按模式分派入队命令。
  - `onCameraPayload(cameraKey, payload)`：接收相机数据并驱动状态机。
  - `handle2dReady()`：READY 窗口内请求 2D 数据。
  - `send3dStart(count)` / `send3dEnd(count, type)`：触发 3D。
  - `tryFinishCycle(count)`：双臂数据就绪后回 `Done`。
- `src/core/DequeueCoordinator.h/.cpp`
  - `onDequeueFrame(PlcDequeueFrame)`：把变化的缓存转成 robot task。
  - `dispatchReadyTasks()`：推送到对应 `RobotWorker`。
  - `onTaskAccepted(RobotTask)`：回写 `XN`。
  - `onTaskFinished(RobotTask, result)`：回写 `XD` 或错误状态。
- `src/core/EventBus.h/.cpp`
  - `publish(Event)`：发布状态、日志、任务事件。
  - `subscribe(EventType, handler)`：UI/diagnostics 订阅。

## 8. io

- `src/io/tcp/TcpServer.h/.cpp`
  - `start(host, port)`：启动监听。
  - `stop()`：关闭监听和连接。
  - `send(connectionId, bytes)`：向指定连接发送。
  - `broadcast(bytes)`：向某类连接广播。
  - `onReadyRead(socket)`：读取并发出 raw frame 事件。
- `src/io/plc/PlcEndpoint.h/.cpp`
  - `start()`：启动 `9999/9090`。
  - `sendDequeueFeedback(bytes)`：通过 `9090` 反馈 `XN/XD`。
  - `sendEnqueueFeedback(text)`：通过 `9999` 反馈 `1Done/2Done/Done`。
  - `status()`：返回连接数、最近指针、端口状态。
- `src/io/camera/CameraEndpoint.h/.cpp`
  - `start()`：启动 2D/3D 相机监听。
  - `sendToCamera(cameraKey, payload)`：发送触发命令。
  - `status()`：返回相机在线状态。
- `src/io/plc/PlcModbusClient.h/.cpp`
  - `connectToPlc(host, port)`：连接 Smart200 Modbus TCP。
  - `disconnectFromPlc()`：断开。
  - `readHoldingRegisters(slave, start, count)`：读保持寄存器。
  - `writeHoldingRegisters(slave, start, values)`：写多个寄存器。
  - `writeCoil(slave, address, value)`：写线圈。
  - `pollConfiguredNodes()`：按地址表轮询。

## 9. robot

- `src/robot/IRobotController.h`
  - `connect()` / `disconnect()`：连接与关闭。
  - `prepare()`：上电、使能、速度和安全状态检查。
  - `enqueueTask(RobotTask)`：异步提交喷涂任务。
  - `stop()` / `pause()` / `resume()`：任务控制。
  - `readStatus()`：读取机器人状态快照。
- `src/robot/duco/DucoRobotClient.h/.cpp`
  - `open()` / `close()`：封装 `DucoCobot::open/close`。
  - `startHeartbeat()` / `stopHeartbeat()`：用独立对象调用 `rpc_heartbeat()`。
  - `powerOn(block)` / `enable(block)` / `disable(block)`：系统控制。
  - `stopAll(block)` / `pauseAll(block)` / `resumeAll(block)` / `abortMotion(block)`：任务控制。
  - `getRobotState()` / `getRobotStatus()` / `getTaskState(taskId)`：状态读取。
  - `moveJ2(joints, v, a, r, block)`：关节相位同步运动。
  - `moveJPose2(pose, v, a, r, qnear, tool, wobj, block)`：进近/回位。
  - `moveL(pose, v, a, r, qnear, tool, wobj, block)`：喷涂直线段。
  - `setToolDigitalOut(num, value, block)` / `setStandardDigitalOut(num, value, block)`：喷枪 IO。
- `src/robot/RobotWorker.h/.cpp`
  - `start()` / `stop()`：启动单 arm worker 线程。
  - `enqueueTask(RobotTask)`：入任务队列。
  - `executeTask(RobotTask)`：按 recipe 执行完整喷涂动作。
  - `executeNoopDefault(RobotTask)`：默认值空任务，安全回 `XN/XD`。
  - `waitForFinished(ret)`：统一判断 DUCO 返回状态。
  - `emitAccepted()` / `emitFinished()` / `emitFailed()`：通知 core。
- `src/robot/MotionPlanner.h/.cpp`
  - `buildTask(ArmPayload, SprayRecipe)`：把相机 payload 转 `RobotTask`。
  - `buildApproach()` / `buildSprayPath()` / `buildRetract()`：生成路径段。
  - `validateTask(RobotTask)`：检查坐标数量、速度、IO。
- `src/robot/FakeRobotController.h/.cpp`
  - `enqueueTask(RobotTask)`：延时模拟 accepted/finished。
  - `setFailureMode(mode)`：测试失败、超时、暂停。
  - `readStatus()`：返回仿真状态。

## 10. ui（只做设计，不在本阶段生成代码）

- `src/ui/MainWindow.h/.cpp`
  - `setupUi()`：创建顶部状态栏、左侧导航、页面栈。
  - `bindViewModels()`：绑定 snapshot service。
  - `onStartClicked()` / `onStopClicked()` / `onEmergencyStopClicked()`：操作命令入口。
- `src/ui/models/QueueTableModel.h/.cpp`
  - `rowCount()` / `columnCount()` / `data()`：展示主队列、arm 队列和缓存。
  - `applySnapshot(QueueSnapshot)`：刷新模型。
- `src/ui/models/EventLogModel.h/.cpp`
  - `append(Event)`：追加事件。
  - `setFilter(filter)`：按设备、等级、count/pointer 过滤。
- `src/ui/pages/OverviewPage.h/.cpp`
  - `refresh(SystemSnapshot)`：设备总览、节拍、当前任务、告警。
- `src/ui/pages/QueuePage.h/.cpp`
  - `refresh(QueueSnapshot)`：主队列、arm1/arm2、发送缓存区。
- `src/ui/pages/DevicePage.h/.cpp`
  - `refresh(DeviceSnapshot)`：PLC、相机、robot、Modbus 连接状态。
- `src/ui/pages/RobotControlPage.h/.cpp`
  - `refresh(RobotSnapshot)`：上电、使能、暂停、恢复、停止、手动测试。
- `src/ui/pages/ConfigPage.h/.cpp`
  - `loadConfig()` / `validatePendingConfig()`：编辑配置和 recipe。
- `src/ui/pages/LogsPage.h/.cpp`
  - `refreshLogs()` / `exportLogs()`：查看和导出通讯日志。

## 11. 界面设计方案

- 顶部固定状态栏：运行/停止、PLC、2D、3D、arm1、arm2、Modbus 状态灯；右侧放急停/停止/暂停恢复按钮。
- 总览页：按流程显示 PLC 入队、相机、队列缓存、机器人执行、PLC 反馈；突出当前 count/pointer。
- 队列页：三块表格，主队列总览、arm1/arm2 明细、发送缓存；颜色区分 pending/camera/default/sent/done/error。
- 设备页：每个设备一张紧凑状态面板，显示 IP、端口、连接时长、最近收发、错误码。
- 机器人页：每个机械臂一个控制面板，提供上电、使能、暂停、恢复、停止、回安全位、读取位姿；危险命令必须二次确认。
- 配置页：编辑 PLC/camera/robot/queue/recipe/modbus 配置，保存前只做校验，不自动热更新关键安全参数。
- 日志页：按入队、出队、相机、robot、modbus 分类；支持十六进制和文本视图切换。

## 12. diagnostics 与测试

- `src/diagnostics/Logger.h/.cpp`
  - `initLogging(config)`：创建滚动日志。
  - `logRawFrame(channel, direction, peer, bytes, note)`：记录原始通讯。
  - `logEvent(Event)`：记录业务事件。
- `src/diagnostics/SnapshotService.h/.cpp`
  - `buildSystemSnapshot()`：合并 core/io/robot 状态。
  - `subscribeUiRefresh()`：按周期推 UI。
- `src/diagnostics/HealthMonitor.h/.cpp`
  - `checkPlc()` / `checkCamera()` / `checkRobot()` / `checkModbus()`：健康检查。
  - `raiseAlarm(code, message)`：生成告警。
- `tests/test_plc_protocol.cpp`：验证 PLC 帧解析和 feedback 编码。
- `tests/test_queue_manager.cpp`：验证成对入队、预取、默认值、晚到丢弃。
- `tests/test_camera_protocol.cpp`：验证 legacy/dual camera 截帧和分流。
- `tests/test_robot_fake_loop.cpp`：验证 fake robot 触发 `XN/XD`。
- `tools/device_simulator/`：C++ 或 Python 设备模拟器，复刻 PLC、相机、fake robot 联调按钮。

