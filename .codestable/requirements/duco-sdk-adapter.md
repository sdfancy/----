---
doc_type: requirement
slug: duco-sdk-adapter
status: current
created: 2026-05-24
last_reviewed: 2026-05-24
tags: [duco, robot, sdk, control]
---

# DUCO SDK 适配能力

## 1. 能力愿景

新 C++ 喷涂控制项目需要用新松 DUCO 远程 API 替代示教器程序和旧机械臂 TCP `data/1` 依赖。适配层必须先稳定完成连接、心跳、上电使能、状态读取和任务控制，为后续喷涂任务执行器提供统一机械臂控制入口。

## 2. 用户故事

- 作为现场调试人员，我可以配置机械臂 IP 和端口，让软件连接 DUCO 控制器并执行上电、使能准备流程。
- 作为设备维护人员，我可以从软件侧发出停止、暂停、恢复命令，并确认命令不阻塞 PLC 和相机通讯回调。
- 作为联调人员，我可以读取机器人状态、程序状态、安全控制器状态和运动中状态，用于 HMI 和日志诊断。
- 作为开发人员，我可以在没有现场 SDK 的电脑上继续编译和测试 fake robot 闭环。

## 3. 边界

- 本能力只封装 DUCO SDK 连接、心跳、上电使能、状态读取和任务控制。
- 本能力引入 `IRobotController` 统一契约，让 fake robot 和 DUCO controller 都可被出队编排使用。
- 当前仓库默认不链接现场 SDK；真实 `DucoCobot` 绑定需等现场 include/lib 后通过 `SPRAY_ENABLE_DUCO` 接入。
- 不解析相机 payload，不生成喷涂轨迹，不调用 `movej_pose2` / `movel` 执行真实运动路径。
- 不修改 PLC `9999/9090`、相机 legacy/dual camera 外部通讯流程。
- 不做 Qt Widgets HMI，不做 Modbus 读写。

## 4. 验收依据

- SDK 未启用时，现有 `spray_control --headless --simulate-robot` 和 `spray_tests.exe` 仍可通过。
- DUCO 相关实现可通过配置或 CMake 开关隔离，缺少现场 include/lib 时不影响 fake robot 构建。
- 任务控制、心跳、阻塞控制调用使用不同 DUCO client 对象。
- PLC 和相机 socket 回调中不直接调用阻塞 DUCO API。

## 5. 变更日志

- 2026-05-24：起草 draft，关联 roadmap item `duco-sdk-adapter`。
- 2026-05-24：实现并验收 `IRobotController`、DUCO role client seam、prepare/control/status 流程和 SDK disabled 构建保护，状态升级为 current。
