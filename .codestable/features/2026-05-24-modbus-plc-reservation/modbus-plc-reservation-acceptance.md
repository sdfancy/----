---
doc_type: feature-acceptance
feature: 2026-05-24-modbus-plc-reservation
status: accepted
summary: 验收 Modbus 旁路预留实现，确认默认禁用、地址表、读写 API 和 PLC 主流程隔离
tags: [modbus, plc, smart200, acceptance]
---

# modbus-plc-reservation 验收报告

> 阶段：阶段 3（验收闭环）
> 验收日期：2026-05-24
> 关联方案 doc：`.codestable/features/2026-05-24-modbus-plc-reservation/modbus-plc-reservation-design.md`

## 1. 接口契约核对

- [x] `ModbusConfig`：`src/config/AppConfig.h/.cpp` 已包含 enabled、host、port、defaultSlaveId、timeoutMs、retries、addressTablePath；默认 `enabled=false`。
- [x] `ModbusNode`：`src/io/plc/ModbusTypes.h` 已包含 name、type、slaveId、address、count、writable、description。
- [x] `ModbusAddressTable`：`load`、`addNode`、`node`、`nodes`、`validate` 已落地，支持空表、重复 name、非法地址和 coil count 校验。
- [x] `PlcModbusClient`：已提供 connect/disconnect、read/write holding registers、write coil、readNode/writeNode。
- [x] CMake 契约：`SPRAY_ENABLE_MODBUS` 默认 OFF，开启时才 `find_package(Qt6 COMPONENTS SerialBus)` 并链接 `Qt6::SerialBus`。
- [x] 流程图：`ModbusAddressTable -> PlcModbusClient -> optional Qt transport -> Smart200` 均有代码落点。

## 2. 行为与决策核对

- [x] 默认禁用：`config/default.toml` 和 `ModbusConfig` 默认 `enabled=false`；禁用时 connect/request 返回 disabled 且 fake transport 无调用。
- [x] 地址表配置化：新增 `config/modbus_nodes.toml`，仅保留注释示例；业务代码无 Smart200 现场地址。
- [x] Qt SerialBus 可选：未启用 `SPRAY_ENABLE_MODBUS` 时使用 unavailable transport，避免本机缺 SerialBus 阻断默认构建。
- [x] 异步边界：真实 transport 通过 `QModbusReply::finished` 完成 callback；不阻塞 PLC/camera socket 回调。
- [x] 写授权：`writeNode` 对 `writable=false` 返回 `PermissionDenied`，不发网络请求。
- [x] 挂载点反向核对：Modbus 引用只落在 CMake、config、`src/io/plc/PlcModbusClient.*`、`ModbusAddressTable.*`、tests 和本 feature 文档。
- [x] 拔除沙盘：移除 CMake 源项、`[modbus]` 配置、地址表、客户端文件和对应测试后，Modbus 能力消失但 `PlcEndpoint` 主流程不需要反向改动。

## 3. 验收场景核对

- [x] S1 默认配置：`disabledConfigRejectsRequestsWithoutTransportCall` 覆盖 disabled 返回和 transport 零调用。
- [x] S2 地址表加载：`ModbusAddressTableTest` 覆盖空表、合法节点、重复 name、非法范围、负数地址、coil count。
- [x] S3 读保持寄存器：`enabledClientConnectsAndRecordsReadWriteRequests` 覆盖 holding read 请求参数。
- [x] S4 写保持寄存器：直接写和 `writeNode` 均有 fake 记录；只读节点拒绝写入。
- [x] S5 写线圈：`writeCoil` 与 coil node 写入均有 fake 记录。
- [x] S6 连接/协议异常：Qt transport 映射 timeout/protocol/transport error；测试覆盖 protocol error 通过 `warning` 信号进入 diagnostics 边界。
- [x] S7 范围守护：grep 确认 `src/app`、`src/core`、`src/robot`、`src/io/camera`、`PlcEndpoint` 无 `PlcModbusClient/QModbus/Modbus` 依赖。
- [x] 构建与测试：ASCII 路径配置和编译通过，`spray_tests.exe` 全套 14 组测试总计 104 passed / 0 failed。

## 4. 术语一致性

- [x] `Modbus sidecar`：代码中表现为独立 `PlcModbusClient`，未挂入 `PlcEndpoint`。
- [x] `AddressTable` / `ModbusNode` / `PlcModbusClient` / `DisabledResult`：命名与 design 第 0 节一致。
- [x] 防冲突：`QModbus*` 只在 `src/io/plc/PlcModbusClient.cpp` 的 `SPRAY_ENABLE_MODBUS` 分支内出现。

## 5. 架构归并

- [x] `ARCHITECTURE.md` 术语表已补 Modbus sidecar、AddressTable、PlcModbusClient。
- [x] `ARCHITECTURE.md` 已新增 `3.5 已落地 Modbus 旁路预留`，说明默认禁用、地址表、客户端 API、Qt SerialBus 可选和 diagnostics warning。
- [x] `ARCHITECTURE.md` 关键决定和硬边界已补：Modbus 默认禁用，地址不得硬编码，`PlcEndpoint` 不持有 `PlcModbusClient`，Modbus 写入不得触发队列/相机/DUCO。

## 6. requirement 回写

- [x] `.codestable/requirements/modbus-plc-reservation.md` 已从 `draft` 升级为 `current`。
- [x] 已追加当前实现：配置、地址表、客户端 API、可选 SerialBus。
- [x] 已追加 2026-05-24 变更日志，保留原能力愿景和边界。

## 7. roadmap 回写

- [x] `cpp-spray-control-items.yaml` 中 `modbus-plc-reservation` 已从 `in-progress` 改为 `done`。
- [x] `cpp-spray-control-roadmap.md` 第 5 节已同步标记 done 和 feature 目录。
- [x] roadmap 变更日志已追加 Modbus 预留完成记录。

## 8. attention.md 候选盘点

- [x] 无新增候选：Qt SerialBus 缺失和中文路径构建问题已在 attention.md 记录；本 feature 没暴露新的每次必读坑。

## 9. 遗留

- 后续优化点：真实 Smart200 地址表、轮询/去抖/快照缓存、HMI 写入确认和现场权限应另起后续 feature。
- 已知限制：默认构建不包含真实 Qt SerialBus transport；需要 `SPRAY_ENABLE_MODBUS=ON` 且本机安装 Qt SerialBus 后才能连接真实 PLC。
- 顺手发现：`Application` 仍未挂载 Modbus 生命周期，这是 design 内允许的 sidecar 预留边界。
