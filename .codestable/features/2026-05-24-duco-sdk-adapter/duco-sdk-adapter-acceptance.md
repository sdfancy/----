---
doc_type: feature-acceptance
feature: 2026-05-24-duco-sdk-adapter
status: passed
accepted_at: 2026-05-24
tags: [duco, robot, sdk, adapter]
---

# duco-sdk-adapter 验收报告

> 阶段：阶段 3（验收闭环）
> 验收日期：2026-05-24
> 关联方案 doc：`.codestable/features/2026-05-24-duco-sdk-adapter/duco-sdk-adapter-design.md`

## 1. 接口契约核对

**接口示例逐项核对**：
- [x] `IRobotController` 示例：`prepare()` / `enqueueTask(task)` -> accepted/finished 驱动 `XN/XD`。代码实际：`IRobotController` 定义统一接口，`DequeueCoordinator` 连接 `taskAccepted/taskFinished` 后回 PLC feedback。
- [x] DUCO role client 示例：heartbeat/control/status 分 role 调用。代码实际：`DucoRobotController` 创建 motion/control/heartbeat/status 四个 client；prepare 调 `heartbeat.rpcHeartbeat`、`control.powerOn/enable`、`status.getRobotState`。

**名词层“现状 -> 变化”逐项核对**：
- [x] `DequeueCoordinator` 从 `FakeRobotController*` 改为 `IRobotController*`，不直接依赖 fake 或 DUCO SDK。
- [x] `FakeRobotController` 实现 `IRobotController`，原 accepted/finished 语义由测试保持。
- [x] `RobotConfig` 增加 mode、ip、port、heartbeat、prepare、auto power/enable 配置。
- [x] `DucoClient` / `IDucoClientFactory` seam 可替换，测试证明 role client 实例隔离。
- [x] `RobotStatus` / `RobotCommandResult` 已落地，承载连接状态、moving、DUCO 状态字段和 message。

**流程图核对**：
- [x] `Application -> IRobotController -> Duco clients`：`Application` 按 robot mode 创建 fake/DUCO controller，`start()` 调 `connectRobot/prepare`。
- [x] `DequeueCoordinator -> IRobotController -> PLC`：出队任务仍走 accepted/finished -> `XN/XD`。

## 2. 行为与决策核对

**需求摘要逐项验证**：
- [x] 统一 controller 契约支持 fake 和 DUCO：`FakeRobotController` 与 `DucoRobotController` 都继承 `IRobotController`。
- [x] DUCO 连接、心跳、上电使能、状态读取、停止、暂停、恢复有适配层入口和测试证据。
- [x] 缺少现场 SDK 时默认 fake 构建不受影响；DUCO mode 可创建 controller，并在 SDK unavailable client open 失败时返回 `DUCO open failed`。

**明确不做逐项核对**：
- [x] 未新增相机 payload 到运动段解析；grep `movej_pose2|movel` 只命中 design/checklist。
- [x] 未修改 PLC `9999/9090` 或相机 legacy/dual camera 外部报文；原应用与相机测试通过。
- [x] 未新增 Qt Widgets、`QModbusTcpClient` 或旧 `data/1` 机械臂 TCP 协议；范围 grep 无代码命中。

**关键决策落地**：
- [x] D1 先抽契约再接 DUCO：`core` 只依赖 `IRobotController`。
- [x] D2 DUCO SDK 可选编译：`SPRAY_ENABLE_DUCO` 默认 OFF，SDK include/lib 仅开关开启时需要。
- [x] D3 client 按 role 隔离：测试 `createsSeparateClientsForEachRole` 证明四个 role 实例不同。
- [x] D4 只做控制面：非默认真实运动任务返回未实现 warning，不做路径执行。

**流程级约束核对**：
- [x] `open()` 未成功前不执行 prepare 后续动作；`ensureConnected()` 失败即返回。
- [x] prepare 顺序固定，测试 `prepareRunsExpectedDucoSequence` 覆盖。
- [x] DUCO `-1` 进入 faulted 并 warning，测试 `prepareFaultsOnNegativeReturn` 覆盖。
- [x] 默认空任务可 accepted/finished；非空任务不产生虚假成功。
- [x] PLC/相机 socket 回调不 include 或调用 DUCO SDK；DUCO 只在 app 组装层和 robot 层出现。

**挂载点反向核对**：
- [x] CMake option：`SPRAY_ENABLE_DUCO`、`SPRAY_DUCO_INCLUDE_DIR`、`SPRAY_DUCO_LIBRARY` 已落地。
- [x] 配置 key：`[robot]` 已落地到 `config/default.toml` 和 `AppConfig`。
- [x] 运行入口：`Application` 根据 robot mode 选择 fake/DUCO。
- [x] robot lifecycle：`Application::start/stop` 接入 connect/prepare/disconnect。
- [x] 拔除沙盘：移除 CMake option、`[robot]` 配置、Application factory/lifecycle 和 `src/robot/duco` 后，本 feature 用户可见能力消失，core/PLC/相机仍可回到 fake-only 旧形态。

## 3. 验收场景核对

- [x] **S1 SDK disabled 构建**：默认构建 `spray_control`、`spray_tests` 成功；`spray_tests.exe` 通过。
- [x] **S2 fake controller 契约兼容**：`FakeRobotLoopTest` 仍证明 `1N1D` 顺序反馈。
- [x] **S3 DUCO prepare 正常路径**：`DucoRobotControllerTest::prepareRunsExpectedDucoSequence` 覆盖 open -> heartbeat -> powerOn -> enable -> status。
- [x] **S4 DUCO 多对象隔离**：`createsSeparateClientsForEachRole` 覆盖四 role 不同实例。
- [x] **S5 DUCO 返回 -1**：`prepareFaultsOnNegativeReturn` 覆盖 faulted + warning。
- [x] **S6 stop/pause/resume**：`taskControlUsesControlClient` 覆盖 control client 调用。
- [x] **S7 PLC/相机流程守护**：`ApplicationLoopTest`、camera endpoint/protocol/workflow 测试通过。

## 4. 术语一致性

- `IRobotController`：代码命中在 robot/core/app 中，含义一致。
- fake robot：保留 `FakeRobotController`，作为 `IRobotController` 实现。
- DUCO controller：代码集中在 `src/robot/duco/DucoRobotController.*`。
- DUCO client role：`DucoClientRole` 定义 motion/control/heartbeat/status。
- 防冲突：禁用词 `QWidget|QModbusTcpClient|data/1|movej_pose2|movel` 无代码命中。

## 5. 架构归并

- [x] `.codestable/architecture/ARCHITECTURE.md`：补入 `IRobotController`、DUCO client role、DUCO SDK 适配骨架、Application robot lifecycle。
- [x] `.codestable/architecture/ARCHITECTURE.md`：补入 `core` 不直接依赖 fake/DUCO SDK 的约束。
- [x] `.codestable/architecture/ARCHITECTURE.md`：补入 `open()` 前置、`-1` faulted、默认不含真实运动路径的边界。

## 6. requirement 回写

- [x] `requirement: duco-sdk-adapter` 从 `draft` 升级为 `current`。
- [x] requirement 保留原能力愿景，补充默认不链接现场 SDK、`SPRAY_ENABLE_DUCO` 后续接入真实 SDK 的边界。

## 7. roadmap 回写

- [x] `.codestable/roadmap/cpp-spray-control/cpp-spray-control-items.yaml`：`duco-sdk-adapter` 从 `in-progress` 改为 `done`，feature 保持 `2026-05-24-duco-sdk-adapter`。
- [x] `.codestable/roadmap/cpp-spray-control/cpp-spray-control-roadmap.md`：第 5 节子 feature 清单同步标记 done，变更日志追加完成记录。
- [x] roadmap items YAML 已通过 `validate-yaml.py` 校验。

## 8. attention.md 候选盘点

- [x] 无候选。本 feature 未暴露新的每次都要知道的环境 / 工具 / 工作流信息；现有 Qt/MinGW ASCII build 目录注意事项仍适用。

## 9. 遗留

- 已知限制：默认构建仍未绑定现场真实 `DucoCobot` SDK，需拿到现场 include/lib 和控制器版本后补具体 client。
- 已知限制：非默认真实运动任务仍不执行运动路径，后续由 `duco-task-executor` 处理 payload -> 运动段。
- 后续观察：`Application` 后续接入 HMI/Modbus 后可能继续变胖，届时可走 `cs-refactor` 抽 service factory 或 `AppContext`。
