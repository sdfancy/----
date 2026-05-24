# qt-hmi-shell 验收报告

> 阶段：阶段 3（design-only 验收闭环）
> 验收日期：2026-05-24
> 关联方案 doc：`.codestable/features/2026-05-24-qt-hmi-shell/qt-hmi-shell-design.md`

## 1. 接口契约核对

- [x] `SystemSnapshot`、`DeviceSnapshot`、`RobotSnapshot`：design 已说明字段来源和边界；本 item 不生成代码。
- [x] `HmiCommandPort`：design 已定义为 UI 命令唯一入口，覆盖 start/stop/emergency/pause/resume/readStatus/readPose/safeHome/configValidate。
- [x] 表格模型：design 已列出 `QueueTableModel::applySnapshot(QueueSnapshot)` 和 `EventLogModel::setFilter(filter)`。
- [x] 建议文件和函数职责：design 第 2.1 节列出 `MainWindow`、`SnapshotProvider`、`HmiCommandPort`、models、pages 的职责。
- [x] 流程图：design 第 2.2 节覆盖 `MainWindow -> SnapshotProvider -> Application -> models` 与 `MainWindow -> HmiCommandPort -> Application/IRobotController`。

## 2. 行为与决策核对

- [x] 需求摘要：本 feature 交付 Qt Widgets HMI 实现蓝图，不生成界面源码。
- [x] D1 Qt Widgets：design 明确工业现场 HMI 使用紧凑、稳定的 Widgets 方案。
- [x] D2 快照只读：design 明确 UI 只读快照，按钮只发送 command port 命令。
- [x] D3 首屏总览：design 明确 GUI 启动后直接进入系统总览页。
- [x] D4 headless 共存：design 明确 `--headless` 继续走 `QCoreApplication`。
- [x] D5 危险命令确认：design 明确急停、停止、回安全位、配置保存等必须二次确认。
- [x] 挂载点：应用入口、UI 模块、快照契约、命令契约、构建挂载点均已在 design 第 2.3 节登记。

## 3. 验收场景核对

- [x] S1 设计产物：design 已覆盖页面结构、快照模型、命令边界、建议文件和函数职责。
- [x] S2 GUI 首屏：design 已规定首屏为系统总览，不做 landing page。
- [x] S3 headless 守护：design 已规定 headless 继续无 Widgets 启动；本 item 未改代码。
- [x] S4 队列展示：design 已规定队列表和缓存表只通过 snapshot 展示。
- [x] S5 危险命令：design 已规定二次确认、经 `HmiCommandPort`、记录事件。
- [x] S6 配置页：design 已规定只校验和提示重启/停机后生效，不热更新危险参数。
- [x] S7 反向核对：本 item 只改 `.codestable` 文档，未改 `src/`、`tests/`、`CMakeLists.txt`。

## 4. 术语一致性

- [x] `HMI shell`、`SnapshotProvider`、`SystemSnapshot`、`DeviceSnapshot`、`RobotSnapshot`、`HmiCommandPort` 均在 design 第 0 节锁定。
- [x] 防冲突：design 明确当前代码没有 `src/ui`，本 feature 只给方案。
- [x] 禁用范围：未新增 `QWidget/QMainWindow` 源码，未新增 `QModbusTcpClient` 实际调用。

## 5. 架构归并

- [x] `ARCHITECTURE.md` 不更新：本 feature 不改变当前代码现状，且 architecture 要记录现状；当前架构文档已说明仍不包含 Qt Widgets。
- [x] roadmap 文档已记录：Qt HMI 方案已完成，代码由后续 feature 或人工实现。
- [x] 后续实现时再把 `src/ui`、快照契约和 command port 写入 architecture。

## 6. requirement 回写

- [x] `qt-hmi-shell` requirement 从 `draft` 升级为 `current`。
- [x] 变更日志追加 design-only 验收记录。
- [x] requirement 边界保留：不生成 UI 代码、不修改 PLC/相机/DUCO 流程、不实现 Modbus 实际读写。

## 7. roadmap 回写

- [x] `cpp-spray-control-items.yaml` 中 `qt-hmi-shell` 从 `in-progress` 改为 `done`。
- [x] `cpp-spray-control-roadmap.md` 第 5 节子 feature 清单同步标记 done。
- [x] roadmap 变更日志追加 `qt-hmi-shell` design-only 完成记录。

## 8. attention.md 候选盘点

- [x] 无候选：本 feature 只交付设计文档，没有新增编译命令、运行步骤或环境坑。

## 9. 遗留

- 后续实现：如果需要我生成 Qt 代码，应另起 UI implementation feature，复用本 design。
- 已知限制：当前仍没有 `src/ui`、`SystemSnapshot`、`DeviceSnapshot`、`RobotSnapshot` 代码实现。
- 顺手发现：`Application.cpp` 后续接入 HMI/Modbus 前仍建议单独评估 service factory 或 `AppContext` 抽取。
