---
doc_type: field-checklist
status: current
feature: 2026-05-24-hardware-acceptance
last_reviewed: 2026-05-24
tags: [hardware, acceptance, checklist]
---

# 真实硬件联调验收 Checklist

Run ID：
日期：
现场负责人：
软件负责人：
PLC 负责人：
相机负责人：
机械臂负责人：
当前 commit：
配置文件：
recipe 文件：
证据目录：

## P0 准备

SafetyHoldPoint：急停、限位、气源、喷枪、工作区已确认。

| 项目 | 结果 | 时间 | 操作者 | 证据 |
|---|---|---|---|---|
| 构建产物存在，Qt/MinGW PATH 已记录 |  |  |  |  |
| DUCO SDK include/lib 与控制器版本已记录 |  |  |  |  |
| 现场配置文件已复制并确认不提交仓库 |  |  |  |  |
| recipe 已替换为现场确认参数 |  |  |  |  |
| 日志持久化策略已确认 |  |  |  |  |

失败处理备注：
签字：

## P1 Dry-run

| 项目 | 结果 | 时间 | 操作者 | 证据 |
|---|---|---|---|---|
| fake robot headless 启动成功 |  |  |  |  |
| PLC 最小闭环得到 `XN/XD` |  |  |  |  |
| dual camera 模式得到 `Done`，或 legacy 模式触发入队成功 |  |  |  |  |
| `events.jsonl` 与 `raw-frames.jsonl` 可保存 |  |  |  |  |

失败处理备注：
签字：

## P2 PLC

| 项目 | 结果 | 时间 | 操作者 | 证据 |
|---|---|---|---|---|
| PLC 连接 `9999` 入队通道 |  |  |  |  |
| PLC 连接 `9090` 出队通道 |  |  |  |  |
| 入队 count/pointer 与软件日志一致 |  |  |  |  |
| 出队指针触发对应 arm 任务 |  |  |  |  |
| 正常完成才回 `XD`，失败不误发 |  |  |  |  |

失败处理备注：
签字：

## P3 相机

| 项目 | 结果 | 时间 | 操作者 | 证据 |
|---|---|---|---|---|
| flow_mode 与现场相机模式一致 |  |  |  |  |
| legacy 或 dual 触发命令发送成功 |  |  |  |  |
| 2D READY / 3D payload 或 legacy 回包进入日志 |  |  |  |  |
| arm1/arm2 payload 写入队列 |  |  |  |  |
| `Done` 或阶段反馈符合既有流程 |  |  |  |  |

失败处理备注：
签字：

## P4 DUCO Prepare

SafetyHoldPoint：不执行喷涂 motion，只做连接、上电、使能、状态读取。

| 项目 | 结果 | 时间 | 操作者 | 证据 |
|---|---|---|---|---|
| DUCO IP/端口与现场控制器一致 |  |  |  |  |
| `open` 成功 |  |  |  |  |
| heartbeat 成功 |  |  |  |  |
| power_on / enable 成功 |  |  |  |  |
| status 可读，失败 warning 可见 |  |  |  |  |

失败处理备注：
签字：

## P5 低速运动

SafetyHoldPoint：低速、无料/空喷、工作区无人、recipe 与喷枪 IO 已确认。

| 项目 | 结果 | 时间 | 操作者 | 证据 |
|---|---|---|---|---|
| 测试 payload 已由现场确认 |  |  |  |  |
| 任务 accepted 后才回 `XN` |  |  |  |  |
| 正常完成后才回 `XD` |  |  |  |  |
| 喷枪 IO 开关与接线一致 |  |  |  |  |
| motion/IO 失败时停机且不误发 `XD` |  |  |  |  |

失败处理备注：
签字：

## P6 生产观察

SafetyHoldPoint：现场负责人同意短时观察和回退方式。

| 项目 | 结果 | 时间 | 操作者 | 证据 |
|---|---|---|---|---|
| count/pointer 全链路一致 |  |  |  |  |
| PLC、相机、DUCO 日志可回溯 |  |  |  |  |
| 设备健康状态可读 |  |  |  |  |
| EvidenceBundle 已收集 |  |  |  |  |
| 待复验项已记录为 issue 或现场问题单 |  |  |  |  |

失败处理备注：
签字：

## EvidenceBundle 清单

- commit 与构建信息：
- config 快照：
- motion recipe 快照：
- logs：
- test-output：
- checklist：
- 现场备注：
- 照片/截图引用：
