---
doc_type: requirement
slug: camera-flow-parity
status: current
created: 2026-05-23
last_reviewed: 2026-05-24
tags: [camera, plc, queue, parity]
---

# 相机入队流程 parity

## 1. 能力愿景

新 C++ 项目必须在不改变 PLC 与相机外部通讯流程的前提下，复刻旧 Python 项目的 legacy 单相机流程和 dual camera `11/12` 流程，让现场 PLC、2D 相机、3D 相机无需重新改协议即可接入新主控。

## 2. 用户故事

- 作为 PLC 联调人员，我发送现有入队命令后，相机触发、队列入队和阶段反馈行为与旧项目一致。
- 作为相机联调人员，我可以继续使用 2D `READY`、`count,type1,type2` 和 3D `(...)EA(...)E` 数据格式。
- 作为调试人员，我可以通过日志追踪 count/pointer 从 PLC 入队到相机 payload 写入队列的全过程。

## 3. 边界

- 保留 PLC `9999` 入队通道字段顺序。
- 保留相机 `9001/9002` 默认端口和 legacy 单相机客户端模式。
- 支持 dual camera `11 -> READY -> 12 -> 3D -> Done` 和 legacy 单相机 sequential/counted 两种匹配。
- 本能力只补入队和相机数据入队，不接真实 DUCO 运动、不做 HMI、不做 Modbus。

## 4. 变更日志

- 2026-05-23：起草 draft，关联 feature `2026-05-23-camera-flow-parity`。
- 2026-05-24：实现并验收 C++ 相机入队 parity，状态升级为 current。
