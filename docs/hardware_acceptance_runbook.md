---
doc_type: field-runbook
status: current
feature: 2026-05-24-hardware-acceptance
last_reviewed: 2026-05-24
tags: [hardware, acceptance, plc, camera, duco]
---

# 真实硬件联调验收 Runbook

本 runbook 用于现场按阶段验证 C++ 喷涂主控。执行顺序必须从 P0 到 P6；任一步失败先停在当前阶段，保存证据后排查，不跳到后续硬件动作。

## 使用规则

- 所有真实硬件阶段必须有现场负责人确认急停、限位、气源、喷枪和机械臂工作区安全。
- 仓库自带 `config/default.toml` 与 `config/motion_recipes.toml` 只是示例，P5 前必须替换为现场确认参数。
- 现场真实 IP、坐标、喷枪 IO、Smart200 地址和凭证不得提交到仓库。
- 证据目录建议使用 `out/field-acceptance/{run_id}`，失败也要保存。

## P0 准备

前置条件：
- 当前 commit、构建目录、Qt/MinGW PATH、DUCO SDK include/lib 和控制器版本已记录。
- 已准备现场配置文件、recipe 文件、日志目录和回退方案。
- 急停、限位、气源、喷枪介质、机械臂工作区已由现场负责人确认。

操作：
- 记录 `git rev-parse --short HEAD`。
- 检查 `config/default.toml` 或现场配置中的 PLC、camera、robot、logging 路径。
- 确认 `[logging] persist_raw_frames=true`、`persist_events=true`，或明确记录为何关闭。
- 确认 recipe 文件不是仓库示例参数。

期望：
- 构建产物存在，配置文件能加载，recipe 文件由现场替换。
- 现场负责人在 checklist P0 签字。

失败处理：
- 配置或 recipe 未确认时，不进入 P1。
- SDK 或构建缺失时，先回到开发机修复构建，不接入真实硬件。

证据：
- commit、配置快照、recipe 快照、SDK/控制器版本、P0 签字记录。

## P1 Dry-run

前置条件：
- P0 通过。
- 不连接真实机械臂动作；使用 fake robot 或本地 simulator。

操作：
- 使用 `spray_control --headless --simulate-robot --config <site-config>` 启动主控。
- 使用现有 `DeviceSimulator` 测试或手工 TCP 客户端复现 PLC 最小闭环。
- dual camera 现场模式启用时，复现 `11 -> READY -> 12 -> 3D -> Done`。

期望：
- PLC 最小闭环能得到 `1N1D` 或对应 arm 的 `XN/XD`。
- dual camera 能得到 `Done`，队列写入 count/pointer 匹配 payload。
- `events.jsonl` 和 `raw-frames.jsonl` 可保存。

失败处理：
- 只排查软件配置、端口占用、日志路径和本地协议；不接入真实硬件补救。

证据：
- dry-run 命令、控制台输出、日志文件、test-output 或手工 TCP 记录。

## P2 PLC

前置条件：
- P1 通过。
- PLC 工程师确认 `9999/9090` 仍按既有协议发送，不启用新的 Modbus 写入。

操作：
- 启动主控并连接真实 PLC `9999/9090`。
- PLC 发送入队命令，记录 count/pointer。
- PLC 发送出队指针，观察 `XN/XD` 反馈。

期望：
- 入队帧、出队帧和反馈 raw frame 均进入日志。
- 指针重复时不重复触发任务。
- 默认空任务仍能安全返回 `XD`。

失败处理：
- PLC 无连接：检查网络、防火墙、端口占用。
- 有帧无反馈：保存 raw frame 和 events，停在 P2 排查队列/反馈链路。

证据：
- PLC 侧发送记录、软件 raw frame、events、队列 snapshot 备注。

## P3 相机

前置条件：
- P2 通过。
- 已确认现场使用 legacy 单相机或 dual camera 模式。

操作：
- legacy：验证 PLC `1/11/12` 触发和相机回包入队。
- dual：验证 `11,count`、2D `READY`、`12,count,type1,type2`、3D `(...)EA(...)E`、`Done`。
- 记录相机 payload 与 count/pointer 对应关系。

期望：
- 相机 payload 按 arm1/arm2 写入队列。
- late payload、半包、格式错误按现有逻辑记录 warning。
- 不改变相机外部协议。

失败处理：
- 触发无响应：检查相机连接方向、端口和 flow_mode。
- payload 不入队：保存 camera raw frame、PLC count/pointer、events。

证据：
- 相机原始回包、软件 raw frame、Done 反馈、队列记录。

## P4 DUCO Prepare

前置条件：
- P3 通过。
- 机械臂工作区清空，急停可用，喷枪处于安全状态。

操作：
- 使用 DUCO 模式启动主控，确认 `open -> heartbeat -> power_on -> enable -> status`。
- 只验证连接和准备，不执行喷涂 motion。
- 记录 robot status 和 warning。

期望：
- DUCO role clients 能连接；prepare 成功后状态可读。
- 失败时进入 faulted 或返回明确错误，不伪造成完成。

失败处理：
- open 失败：检查 IP、端口 7003、防火墙、SDK 版本。
- power/enable/status 失败：停机，保存 warning 和控制器状态，由现场机器人负责人处理。

证据：
- 控制器版本、robot status、events、warning、现场截图或记录。

## P5 低速运动

前置条件：
- P4 通过。
- SafetyHoldPoint：现场负责人确认低速、无料/空喷、工作区无人、喷枪 IO 接线和 recipe 参数。
- recipe 文件必须为现场替换值，不是仓库示例。

操作：
- 发送一组已确认的低速测试 payload。
- 观察 accepted/finished、`XN/XD`、DUCO motion 顺序和喷枪 IO 开关。
- 人工确认异常时能停止/急停，并保存日志。

期望：
- 任务 accepted 后执行最小序列，正常完成才回 `XD`。
- motion 或 IO 失败时不误发正常 `XD`，并记录 warning。
- 喷枪 IO 行为与现场接线一致。

失败处理：
- 任一 motion/IO 异常立即停止，不继续生产观察。
- 保存 events、raw frames、recipe、payload 和现场备注。

证据：
- 测试 payload、recipe、robot events、PLC feedback、现场签字。

## P6 生产观察

前置条件：
- P5 通过。
- 现场负责人同意短时观察窗口和回退方式。

操作：
- 按现场低速/短时节拍运行一小段真实流程。
- 观察 PLC、相机、DUCO、日志、设备健康和队列状态。
- 结束后运行证据收集 helper 或手工整理证据目录。

期望：
- count/pointer 链路一致，异常可通过日志定位。
- 设备健康、raw frame 和 events 可回溯。
- checklist 完整记录通过、失败或待复验项。

失败处理：
- 停止观察，保留当前证据目录。
- 对失败项另起 issue，不在现场临时修改协议或硬编码参数。

证据：
- 完整 EvidenceBundle、现场 checklist、问题列表和负责人签字。
