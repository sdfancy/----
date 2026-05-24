---
doc_type: requirement
slug: qt-hmi-shell
status: current
created: 2026-05-24
last_reviewed: 2026-05-24
tags: [qt, hmi, ui, snapshot]
---

# Qt HMI 界面壳能力

## 1. 能力愿景

新 C++ 喷涂控制项目需要一个 Qt Widgets 工业 HMI，用于现场操作员和联调人员查看队列、PLC、相机、机械臂和日志状态，并通过高层命令执行启动、停止、暂停、恢复、急停等操作。HMI 只能读取系统快照和发送命令，不直接修改队列容器，也不改变 PLC、相机、新松机械臂的既有通讯流程。

当前 roadmap item 只输出界面设计和实现蓝图，不生成 Qt 界面代码。

## 2. 用户故事

- 作为操作员，我可以在首屏看到系统运行状态、PLC/相机/机械臂连接状态、当前 count/pointer 和告警。
- 作为 PLC/相机联调人员，我可以查看主队列、双臂队列、出队缓存和最近收发事件，确认指针、count 和默认值策略是否正确。
- 作为机械臂维护人员，我可以查看 arm1/arm2 状态，并通过确认后的高层按钮执行停止、暂停、恢复、读取状态等命令。
- 作为软件维护人员，我可以查看分类日志和配置校验结果，定位通讯或任务执行问题。

## 3. 边界

- 不生成 `src/ui` 代码，不修改 `CMakeLists.txt`，不引入 Qt Widgets 链接项。
- 不修改 PLC `9999/9090` 协议、相机 legacy/dual camera 流程或 DUCO motion 执行语义。
- UI 不直接调用 `QueueManager` 写接口，不直接访问 socket，不直接持有 `DucoCobot`。
- 不实现 Smart200 Modbus 读写；仅在界面设计中预留状态灯和配置入口。
- 不实现完整 recipe 编辑、热更新或危险动作细节；危险命令必须走确认和 command port。
- 不把原始通讯日志刷到控制台；后续实现时只通过日志文件和日志页查看。

## 4. 验收依据

- 设计文档明确主窗口、页面、模型、快照和命令边界。
- 设计文档列出后续建议文件和函数职责，足够人工按方案实现界面。
- 设计文档说明 GUI 与 `--headless` 的共存方式，确保无界面主控不被破坏。
- 设计文档包含正常、边界和错误场景验收点。
- 本 item 不应改动 `src/`、`tests/` 或构建脚本。

## 5. 变更日志

- 2026-05-24：起草 draft，关联 roadmap item `qt-hmi-shell`。
- 2026-05-24：完成 design-only 验收，输出 Qt HMI 页面、快照模型和命令边界蓝图，状态升级为 current。
