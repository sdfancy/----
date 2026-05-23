---
doc_type: requirement
slug: cpp-minimal-loop
status: current
created: 2026-05-23
last_reviewed: 2026-05-23
tags: [cpp, plc, queue, fake-robot]
---

# C++ 最小闭环能力

## 1. 能力愿景

新 C++ 喷涂控制项目必须先具备一条不依赖真实机械臂和相机的最小闭环，用来验证 PLC 双端口、队列核心、出队缓存和 `XN/XD` 反馈语义在 C++/Qt 运行时下稳定成立。

## 2. 用户故事

- 作为调试人员，我可以启动 `spray_control --headless --simulate-robot`，看到程序监听 PLC `9999/9090`。
- 作为 PLC 联调人员，我可以发送入队帧 `command,count,pointer`，确认软件创建 arm1/arm2 双队列项。
- 作为 PLC 联调人员，我可以发送出队双指针，确认软件只对变化的非零指针创建任务，并在 fake robot accepted/finished 后收到 `XN/XD`。

## 3. 当前边界

- 已实现 PLC 入队 6 字节大端解析、PLC 出队 4 字节大端解析和 `1N/1D/2N/2D` 编码。
- 已实现 C++ 队列核心、出队缓存、默认 payload、fake robot accepted/finished 和 headless 启动。
- 未接入真实 DUCO SDK、Qt Widgets HMI、相机 `9001/9002` 和 Modbus。

## 4. 验收依据

- `spray_tests.exe` 全部通过。
- headless 启动输出 `plc.enqueue 0.0.0.0 9999` 和 `plc.dequeue 0.0.0.0 9090`。
- 反向 grep 无 `DucoCobot`、Qt Widgets、`QModbusTcpClient`、相机 `9001/9002` 或旧 `9009` 命中。

## 5. 变更日志

- 2026-05-23：backfill 为 current；对应 feature `2026-05-23-cpp-minimal-loop` 已实现并通过验收。
