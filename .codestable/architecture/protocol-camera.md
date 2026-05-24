---
doc_type: architecture
status: current
created: 2026-05-24
last_reviewed: 2026-05-24
tags: [camera, protocol, queue, plc]
---

# 相机协议与入队流程

> 状态：当前现状
> 创建日期：2026-05-24

## 1. 范围

本文记录 C++ 主控已落地的相机入队协议。目标是保持旧 Python 项目的 PLC/相机外部流程不变，只把通讯、解析和状态机迁移到 C++/Qt。

## 2. 配置

- `camera.flow_mode = "legacy_single_camera"`：legacy 单相机模式，软件作为 TCP client 连接 `legacy_host:legacy_port`。
- `camera.flow_mode = "dual_camera_11_12"`：dual camera 模式，软件监听 `camera2d_port` 和可选 `camera3d_port`。
- 默认端口保留 2D `9001`、3D `9002`。
- `camera_correlation_mode` 支持 `sequential` / `counted`。
- `camera_count_extract_mode` 支持 `auto` / `ascii` / `binary` / `disabled`。

## 3. dual camera 流程

1. PLC 在 `9999` 发送 6 字节大端入队帧 `command,count,pointer`，`command=11`。
2. `EnqueueWorkflow` 成对创建 arm1/arm2 队列项，打开 2D READY 窗口，向 3D 发送 `11,count`。
3. 2D 发送 `READY` 后，软件向 2D 发送 `(count)E`；同一窗口重复 READY 不重复发送。
4. 2D 返回 `count,type1,type2` 后，软件向 2D 回 `OK` 并暂存类型。
5. PLC 发送 `command=12` 后，软件向 3D 发送 `12,count,type1,type2`；没有 2D 类型时用 `00,00`。
6. 3D 返回 `(...)EA(...)E` 后，软件按 flag 千位分流 arm1/arm2，写入 `QueueManager::storeCameraData()`。
7. 同一 count 的 arm1/arm2 payload 都写入后，软件在 PLC 入队连接回 `Done`，且只回一次。

## 4. legacy 单相机流程

1. PLC 入队帧仍由 `command,count,pointer` 驱动。
2. 软件向单相机发送 2 字节 command + 2 字节 count 的大端二进制触发帧。
3. 回包按 payload 中 flag 千位识别 arm1/arm2。
4. `sequential` 模式使用当前等待槽写队列；相机不带 count 时会把槽位 count 插入 payload。
5. `counted` 模式按回包 count 查找 pointer，允许乱序回包。
6. 写入成功后，通过 PLC 入队连接回 `1Done` 或 `2Done`。

## 5. 约束

- `QueueManager` 不实现相机状态机，只保存相机 payload。
- socket 回调只读写 bytes、发信号和触发状态机，不做机器人阻塞运动。
- 3D 半包保留在协议缓冲区，直到出现完整 `(...)EA(...)E`。
- 非法相机段记录 WARN，进程继续运行。
- 相机未连接时发送失败只记录 WARN，不阻塞 PLC 通讯。
