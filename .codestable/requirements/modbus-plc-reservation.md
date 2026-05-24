---
doc_type: requirement
slug: modbus-plc-reservation
status: current
created: 2026-05-24
last_reviewed: 2026-05-24
tags: [modbus, plc, smart200, qt]
---

# Smart200 Modbus 预留能力

## 1. 能力愿景

新 C++ 喷涂控制项目后续需要从 Siemens Smart200 PLC 读取和修改寄存器、线圈等数据。该能力先建立默认禁用的 Modbus TCP 客户端、地址表配置和读写 API，作为 HMI、规则引擎或现场调试工具的旁路能力；它不得参与或改变现有 PLC `9999/9090` TCP 主流程。

## 2. 用户故事

- 作为现场联调人员，我可以通过配置的地址名读取 Smart200 寄存器，确认 PLC 状态和现场变量。
- 作为维护人员，我可以在明确授权后写寄存器或线圈，用于后续调试和参数调整。
- 作为软件开发人员，我可以在没有真实 PLC 的环境下验证地址表解析、禁用状态和读写请求编排。
- 作为 PLC 工程师，我可以确认 Modbus 不影响原有入队、出队和 `XN/XD` 反馈链路。

## 3. 边界

- Modbus 默认禁用，禁用时不连接 PLC、不轮询、不写任何 PLC 数据。
- 不修改 PLC `9999/9090` 协议、端口、报文或 `PlcEndpoint` 出入队语义。
- 不把 Smart200 地址硬编码到业务代码；地址必须来自配置或地址表文件。
- 不在本能力内定义具体业务含义，真实地址表需要现场 PLC 工程师提供。
- 不通过 Modbus 触发机械臂运动；机械臂执行仍由队列出队和 DUCO controller 驱动。
- 不做 HMI 页面，只提供后续 HMI 可调用的 API 形态。

## 4. 验收依据

- 配置层能表达 Modbus enabled、host、port、slaveId、timeout、retry 和地址表路径。
- 地址表能表达 holding register、coil 等节点，不要求现场真实地址。
- 读写 API 契约覆盖读保持寄存器、写保持寄存器、写线圈。
- 禁用状态下调用 API 返回可观察的 disabled 错误，不产生网络连接。
- 反向核对确认 `9999/9090`、相机流程和 DUCO 执行路径不变。

## 5. 当前实现

- `ModbusConfig` 已接入 `AppConfig` 和 `config/default.toml`，默认 `enabled=false`。
- `config/modbus_nodes.toml` 已提供空表示例文件，不包含现场真实 Smart200 地址。
- `ModbusAddressTable` 已支持节点加载、按名查找和范围校验。
- `PlcModbusClient` 已支持连接、断开、读保持寄存器、写保持寄存器、写线圈和按节点读写。
- `SPRAY_ENABLE_MODBUS` 默认关闭；未安装 Qt SerialBus 时仍可构建和运行现有主流程测试。

## 6. 变更日志

- 2026-05-24：起草 draft，关联 roadmap item `modbus-plc-reservation`。
- 2026-05-24：完成实现并升级为 current，保留默认禁用和 PLC 主流程隔离边界。
