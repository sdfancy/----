---
doc_type: explore
type: question
date: 2026-05-19
slug: plc-dequeue-cache-pointer
topic: PLC 入队/出队指针与机械臂缓存创建关系
scope: src/core/queue_manager.py, src/core/plc_handler.py, src/utils/data_parser.py, config/settings.py, logs/system.log
keywords:
  - PLC
  - 入队指针
  - 出队指针
  - 出队缓存
  - prefetch_offset
  - "(1000,0)E"
status: active
confidence: high
---

## 问题与范围

用户问题：PLC 入队和出队指针是否必须一样，机械臂才能正确从软件中读取数据；当前现场现象是不创建缓存，机械臂读到 `(1000,0)E`。

本次只查软件现状，不修改逻辑。

## 速答

不是要求“PLC 入队指针”和“PLC 出队指针在同一时刻必须相等”。软件真正要求的是：PLC 出队指针必须能在软件当前内存队列里找到对应的入队槽位；并且当前配置 `prefetch_offset=1`，找到目标槽位后实际下发的是目标后一拍的数据。

所以在当前配置下，缓存创建条件更准确地说是：

1. PLC 出队报文的对应机械臂指针发生变化。
2. 变化后的出队指针已经存在于该机械臂队列。
3. 因为 `prefetch_offset=1`，该目标指针之后还要有下一条已入队数据，才能创建发送缓存。

`(1000,0)E` 不是某个真实工件的数据，而是 arm1 在“data 请求时无缓存”的默认值；`count=0` 表示默认/无数据。

```mermaid
flowchart LR
  A[PLC 入队 6 字节: command/count/pointer] --> B[按 pointer 创建 arm1/arm2 队列槽位]
  C[PLC 出队 4 字节: arm1_ptr/arm2_ptr] --> D{指针是否变化}
  D -- 否 --> E[不触发缓存更新]
  D -- 是 --> F{目标 pointer 是否在队列中}
  F -- 否 --> G[NO_DATA_AVAILABLE, 不创建缓存]
  F -- 是 --> H{prefetch_offset=1 时下一拍是否存在}
  H -- 否 --> I[prefetch_wait, 不创建缓存]
  H -- 是 --> J[创建 arm_caches 缓存]
  K[机械臂 data] --> L{是否有缓存}
  L -- 否 --> M[返回默认值如 (1000,0)E]
  L -- 是 --> N[返回缓存数据]
```

## 关键证据

- 入队报文解析为 `command + count + enqueue_pointer`，软件把第三个字段作为入队指针：`src/utils/data_parser.py:73-95`。
- 入队时软件按同一个 `pulse_pointer` 给 arm1/arm2 各创建一个队列项，队列 key 就是这个 pointer：`src/core/queue_manager.py:451-486`。
- 出队通道固定读 4 字节，解析为 arm1/arm2 两个出队指针；只有变化项才回调队列逻辑：`src/core/plc_handler.py:248-316`。
- 队列侧也只对变化后的出队指针调用 `_process_arm_dequeue_locked`；如果目标 pointer 不存在，直接 `NO_DATA_AVAILABLE` 并返回，不创建缓存：`src/core/queue_manager.py:687-748`。
- 当前配置 `prefetch_offset=1` 且 `send_default_on_empty_request=True`：`config/settings.py:139-146`。
- 预取逻辑会选择目标后一拍；如果后一拍不存在，返回 `None`，调用方会删除缓存并标记 `NO_DATA_AVAILABLE`：`src/core/queue_manager.py:750-764`、`src/core/queue_manager.py:801-836`。
- 机械臂发 `data` 时如果没有缓存，且允许默认值，软件会生成默认 payload；`count=None` 会变成 `(1000,0)E` / `(2000,0)E`：`src/core/queue_manager.py:181-200`、`src/core/queue_manager.py:872-895`。
- 现场日志里有一次明确例子：程序 14:25:08 重启后只入队了 `pointer=2`，14:25:09 出队却找 `arm1 pointer=7` 和 `arm2 pointer=8`，因此都找不到队列项：`logs/system.log:59490-59503`。

## 细节展开

这里的软件是“按 pointer 查槽位”，不是按 count 查槽位。count 会存进队列项并用于默认值、历史和完成确认，但出队缓存创建的第一步是 `self.arm_data_queues[arm_id].get(target_pointer)`。

当前 `prefetch_offset=1` 会让“出队指针”和“实际下发数据指针”错开一拍。例如 PLC 出队指针触发 `7`，软件会先找到 pointer=7 的槽位，再尝试下发 enqueue 顺序里的下一条，也就是通常意义上的 pointer=8。如果只有 pointer=7 没有 pointer=8，则不会创建缓存。

因此“入队指针和出队指针必须一样”这句话不够准确。更准确的是：出队指针必须落在已经入队且还没被淘汰的指针集合里；如果启用预取，目标后一拍也必须已经入队。

当前 `(1000,0)E` 对应的是“机械臂请求时无缓存”的默认值路径。文档也明确 `count=0` 表示默认值，不代表真实计数。

## 未决问题

当前没有看到 PLC 侧真实在线指针寄存器，只能从软件源码和日志判断。还需要确认 PLC 重启、软件重启、队列清空后，PLC 出队指针是否会从旧值继续发，导致软件内存队列里没有对应 pointer。

## 后续建议

下一步应对照 PLC 在线值确认：软件刚启动或队列清空后，PLC 出队指针是否仍是 7/8，而入队指针只刚恢复到 2；如果是，这就是本次不创建缓存的直接原因。

## 相关文档

- `软件数据管理流程说明.md:82-105`
- `软件数据管理流程说明.md:202-203`
