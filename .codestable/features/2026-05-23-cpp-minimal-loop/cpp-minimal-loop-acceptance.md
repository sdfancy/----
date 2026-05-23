# cpp-minimal-loop 验收报告

> 阶段：阶段 3（验收闭环）
> 验收日期：2026-05-23
> 关联方案 doc：`.codestable/features/2026-05-23-cpp-minimal-loop/cpp-minimal-loop-design.md`

## 1. 接口契约核对

- [x] `parseEnqueueFrame(QByteArray::fromHex("0001000A0001"))`：代码在 `src/protocol/PlcProtocol.cpp` 解析为 `command=1,count=10,pointer=1`；`tests/test_plc_protocol.cpp` 覆盖。
- [x] `parseDequeueFrame(QByteArray::fromHex("00010000"))`：代码解析为 `arm1Pointer=1,arm2Pointer=0`；`tests/test_plc_protocol.cpp` 覆盖。
- [x] `buildFeedback(1, Normal/Done)`：代码返回 `1N/1D`，并覆盖 `2N/2D`。
- [x] 名词层变化：`PlcEnqueueFrame`、`PlcDequeueFrame`、`FeedbackStage`、`ArmQueueItem`、`DequeueCache`、`RobotTask`、fake robot 均有代码落点。
- [x] 流程图核对：`PlcEndpoint -> QueueManager -> DequeueCoordinator -> FakeRobotController -> PlcEndpoint feedback` 已在 `Application::wireEvents()` 和 `DequeueCoordinator` 中落地。

## 2. 行为与决策核对

- [x] 需求摘要：headless 程序可监听 `9999/9090`，解析 PLC 入队/出队，维护双队列，fake robot 回 `1N/1D/2N/2D`。
- [x] 明确不做：反向 grep `DucoCobot|QWidget|QMainWindow|QModbusTcpClient|9001|9002|9009` 对 `CMakeLists.txt src tests config` 无命中。
- [x] 关键决策 D1：使用 `QCoreApplication` headless，不创建 Widgets。
- [x] 关键决策 D2：fake robot 替代 DUCO，`FakeRobotController` 用 timer 模拟 accepted/finished。
- [x] 关键决策 D3：出队即调度任务，`DequeueCoordinator::onDequeueFrame()` 提交任务。
- [x] 关键决策 D4：相机不进本闭环；仅 `QueueManager::storeCameraData()` 作为测试注入入口。
- [x] 流程级约束：重复指针不重复触发、0 指针跳过、同 arm 串行、无 9090 连接只 WARN、默认 payload 标记 `defaultNoop=true` 均有测试。
- [x] 挂载点：`CMakeLists.txt`、`config/default.toml`、`spray_control --headless --simulate-robot`、`PlcEndpoint` 端口注册均已落地。反向 grep 未发现方案外挂载点需要补记。

## 3. 验收场景核对

- [x] S1 启动 headless：手工运行输出 `plc.enqueue 0.0.0.0 9999` 和 `plc.dequeue 0.0.0.0 9090`。
- [x] S2 入队帧 `00 01 00 0A 00 01`：`ApplicationLoopTest` 和 `QueueManagerTest` 证明创建 pointer=1、count=10。
- [x] S3 出队帧 `00 01 00 00`：`FakeRobotLoopTest` 证明 PLC 收到 `1N1D`。
- [x] S4 重复出队帧：`QueueManagerTest::repeatedPointerDoesNotCreateDuplicateTask` 通过。
- [x] S5 出队帧 `00 01 00 01`：`QueueManagerTest::eachArmChangesIndependently` 证明 arm2 独立触发。
- [x] S6 非法长度入队帧：`PlcProtocolTest::rejectsShortEnqueueFrame` 和应用层错误分支覆盖。
- [x] S7 `9090` 无连接：`FakeRobotLoopTest::noConnectionOnlyMarksFeedbackFailed` 证明不崩溃且反馈失败可观察。

## 4. 术语一致性

- `PlcEndpoint`、`QueueManager`、`DequeueCoordinator`、`FakeRobotController`、`PlcEnqueueFrame`、`PlcDequeueFrame`、`FeedbackStage`、`RobotTask` 与方案术语一致。
- 防冲突：未引入 `data/1` 旧机械臂协议、DUCO SDK、相机端口或 Modbus 术语到最小闭环代码。

## 5. 架构归并

- [x] `.codestable/architecture/ARCHITECTURE.md` 已更新：补入最小闭环模块落点、协议、队列、PLC endpoint、fake robot 和约束。
- [x] 方案第 4 节要求的最小闭环现状已归并到架构入口。
- [x] `attention.md` 已记录 Windows/Qt MinGW 构建路径约束。

## 6. requirement 回写

- [x] 方案 frontmatter `requirement` 为空，但这是用户可见的新能力；已 backfill `.codestable/requirements/cpp-minimal-loop.md`，状态为 `current`。

## 7. roadmap 回写

- [x] `roadmap: cpp-spray-control` / `roadmap_item: cpp-minimal-loop` 已处理。
- [x] `.codestable/roadmap/cpp-spray-control/cpp-spray-control-items.yaml` 中 `cpp-minimal-loop` 已改为 `done`。
- [x] roadmap 主文档第 5 节子 feature 清单已同步标记 done，并追加变更日志。

## 8. attention.md 候选盘点

- [x] 已落入 attention.md：Windows/Qt MinGW 构建需要设置 PATH，中文源码路径下项目内 build 会触发 Qt `moc` 失败，建议 ASCII 构建目录。

## 9. 遗留

- 当前最小闭环不接真实 DUCO SDK、相机流程、Qt Widgets HMI 和 Modbus，按 roadmap 后续 feature 继续。
- 当前目录不是 git 仓库；如需提交，需要先初始化 git 或在已有仓库中添加本目录。
