---
doc_type: requirement
slug: duco-task-executor
status: current
created: 2026-05-24
last_reviewed: 2026-05-24
tags: [duco, robot, motion, spray]
---

# DUCO 任务执行能力

## 1. 能力愿景

新 C++ 喷涂控制项目需要把已入队的相机 payload 转成新松 DUCO 运动任务，替代旧示教器程序和旧机械臂 TCP `data/1` 协议。能力完成后，PLC 出队指针仍按现有 `9999/9090` 流程进入软件；软件在 DUCO 任务开始时反馈 `XN`，在任务正常结束或默认空任务安全结束时反馈 `XD`。

## 2. 用户故事

- 作为现场联调人员，我可以用真实相机 payload 触发 DUCO 运动执行，而不再依赖机械臂示教器程序。
- 作为 PLC 联调人员，我可以继续使用原有出队指针和 `XN/XD` 反馈时序，不修改 PLC 主流程。
- 作为设备维护人员，我可以在 payload、recipe 或 DUCO 返回异常时看到明确告警，并确认软件不会把失败伪装成正常完成。
- 作为开发人员，我可以在没有真实 DUCO SDK 的环境下用 mock DUCO client 验证规划顺序、IO 顺序和失败路径。

## 3. 边界

- 本能力只处理出队 payload 到 DUCO 运动段的解析、规划、执行和结果反馈。
- 已落地最小 `MotionRecipe` 注入能力，用于测试和执行链路；完整现场 recipe 文件管理仍属于 `field-config-and-recipes`。
- 不修改 PLC `9999/9090` 和相机 legacy/dual camera 外部报文。
- 不恢复旧机械臂 `data/1` TCP 协议，不让示教器程序继续承担任务执行。
- 不做 Qt HMI、不做 Smart200 Modbus、不做现场 recipe UI。
- 现场坐标字段、速度、工具坐标系、工件坐标系、喷枪 IO 只能来自配置或测试注入，禁止硬编码到执行器。

## 4. 验收依据

- 默认空任务 `(1000,count)E` / `(2000,count)E` 仍直接 accepted/finished，并驱动 `XN/XD`。
- 有效 payload + recipe 可生成确定的 `movej_pose2 -> IO on -> movel... -> IO off` 序列。
- 无效 payload、缺失 recipe、DUCO 返回 `-1` 时不调用后续危险运动，发出 warning，并不发送正常完成 `XD`。
- `stop/pause/resume/readStatus` 继续走 control/status client，不与 motion client 混用。
- 现有 PLC、相机、fake robot 测试继续通过；默认构建不强制链接现场 DUCO SDK。

## 5. 变更日志

- 2026-05-24：起草 draft，关联 roadmap item `duco-task-executor`。
- 2026-05-24：实现并验收 payload 解析、motion 规划、DUCO motion/IO seam、DUCO task execution、失败不误发 `XD`，状态升级为 current。
