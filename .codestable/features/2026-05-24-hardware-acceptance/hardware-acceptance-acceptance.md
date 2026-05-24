---
doc_type: feature-acceptance
feature: 2026-05-24-hardware-acceptance
status: accepted
accepted_at: 2026-05-24
summary: 验收真实硬件联调 runbook、现场 checklist 和证据收集 helper；现场设备执行结果待现场填写
tags: [hardware, acceptance, plc, camera, duco]
---

# hardware-acceptance 验收报告

> 阶段：阶段 3（验收闭环）
> 验收日期：2026-05-24
> 关联方案 doc：`.codestable/features/2026-05-24-hardware-acceptance/hardware-acceptance-design.md`

## 1. 接口契约核对

**接口示例逐项核对**：
- [x] `tools/field_acceptance/collect_evidence.ps1 -RunId ...`：实际参数包含 `RunId`、`OutputRoot`、`ConfigPath`、`RecipePath`、`LogDir`、`TestOutputDir`、`ChecklistPath`；输出目录为 `out/field-acceptance/{run_id}`，含 `config/`、`logs/`、`test-output/`、`summary.txt` 和 checklist 复制件。

**名词层“现状 -> 变化”逐项核对**：
- [x] `FieldRunbook`：落地为 `docs/hardware_acceptance_runbook.md`，覆盖 P0-P6。
- [x] `FieldChecklist`：落地为 `docs/hardware_acceptance_checklist.md`，包含 pass/fail、时间、操作者、证据、SafetyHoldPoint 和签字位。
- [x] `EvidenceBundle`：helper 复制 config、recipe、logs、test-output、checklist，并写入 commit 和 missing/copied 汇总。
- [x] 低风险 helper：脚本只做文件复制和 summary，不包含 PLC、相机、DUCO、Modbus 连接或 motion/IO 命令。

**流程图核对**：
- [x] 图中 `Field Operator -> Runbook -> spray_control -> Diagnostics Logs -> PLC/Camera/DUCO` 的职责均在 runbook/checklist 中有人工执行和证据记录落点；helper 只负责证据归档。

## 2. 行为与决策核对

**需求摘要逐项验证**：
- [x] P0-P6 分阶段 gate 已写入 runbook；后一步不能掩盖前一步失败。
- [x] 每阶段包含前置条件、操作、期望、失败处理和证据。
- [x] 真实硬件执行不在本地伪造；报告和 requirement 均记录现场结果待 checklist 填写。

**明确不做逐项核对**：
- [x] 未修改 PLC `9999/9090` 字节格式、反馈码或端口默认值；本 feature 无 `src/protocol`、`src/io`、`src/core`、`src/robot` 运行时代码 diff。
- [x] 未修改相机 legacy/dual camera 外部协议。
- [x] 未新增自动执行真实 motion/IO 的脚本；grep 只命中文档中的禁止/说明文字。
- [x] 未提交真实现场 IP、坐标、喷枪 IO、Smart200 地址或凭证；grep 只命中边界说明。
- [x] 未新增 Qt Widgets 页面或 HMI 代码；Qt HMI 用户代码已单独提交为 `5d841c1`，本 feature diff 不包含 `src/ui`。

**关键决策落地**：
- [x] 验收包优先于自动化：helper 不连设备，不发动作。
- [x] 分阶段 gate：runbook/checklist 均按 P0-P6 固定顺序组织。
- [x] 证据统一归档：helper 生成 EvidenceBundle 目录和 `summary.txt`。
- [x] 硬件差异留给现场参数：文档明确禁止提交真实 IP、坐标、IO、Smart200 地址和凭证。
- [x] 失败优先停机：P2-P6 均写入失败处理和停止/回退要求。

**挂载点反向核对（可卸载性）**：
- [x] runbook 挂载点：`docs/hardware_acceptance_runbook.md`。
- [x] checklist 挂载点：`docs/hardware_acceptance_checklist.md`。
- [x] evidence helper 挂载点：`tools/field_acceptance/collect_evidence.ps1`。
- [x] docs/roadmap/req 挂载点：本 acceptance 已更新 architecture、requirement、VISION、roadmap 和 checklist。
- [x] 反向核查：本 feature 引用集中在 `.codestable/features/2026-05-24-hardware-acceptance/`、`.codestable/requirements/hardware-acceptance.md`、`docs/hardware_acceptance_*`、`tools/field_acceptance/`、architecture/roadmap/VISION 回写。
- [x] 拔除沙盘推演：删除 runbook、checklist、helper 和上述 CodeStable 回写后，不会残留运行时代码行为。

## 3. 验收场景核对

- [x] S1 准备阶段：runbook P0 覆盖构建、SDK、配置、日志、急停、气源、工作区和 recipe 替换。
- [x] S2 dry-run 阶段：runbook P1 覆盖 fake robot、DeviceSimulator、PLC 最小闭环、dual camera `Done` 和日志保存。
- [x] S3 PLC 阶段：runbook P2 覆盖 `9999/9090`、count/pointer、`XN/XD`、重复指针和 raw frame 证据。
- [x] S4 相机阶段：runbook P3 覆盖 legacy/dual、READY、payload、Done、late/半包 warning。
- [x] S5 DUCO prepare 阶段：runbook P4 覆盖 open、heartbeat、power_on、enable、status，明确不执行喷涂路径。
- [x] S6 低速运动阶段：runbook P5 覆盖 SafetyHoldPoint、现场 recipe、accepted/finished、IO 和失败停机。
- [x] S7 证据收集：helper smoke test 已通过，可生成 EvidenceBundle；现场真实执行结果待现场填写。

## 4. 术语一致性

- `FieldRunbook`、`FieldChecklist`、`EvidenceBundle`、`DryRunGate`、`SafetyHoldPoint` 已归并到 architecture 术语表。
- 禁止项术语保持一致：PLC/相机协议不改、真实 motion/IO 不自动执行、现场参数不入库。
- `hardware-acceptance-checklist.yaml` 中所有 acceptance checks 已从 `pending` 更新为 `passed`。

## 5. 架构归并

- [x] `.codestable/architecture/ARCHITECTURE.md`：补入 FieldRunbook、FieldChecklist、EvidenceBundle、DryRunGate、SafetyHoldPoint。
- [x] `.codestable/architecture/ARCHITECTURE.md`：新增 3.8 硬件验收包现状，说明 runbook/checklist/helper 形态。
- [x] `.codestable/architecture/ARCHITECTURE.md`：补入硬边界，helper 不得自动执行真实 motion、IO 或 Modbus 写入，现场参数不得提交。

## 6. requirement 回写

- [x] `.codestable/requirements/hardware-acceptance.md` 已由 `draft` 升级为 `current`，并记录 `implemented_by: [2026-05-24-hardware-acceptance]`。
- [x] `.codestable/requirements/VISION.md` 已将 `hardware-acceptance` 从 Draft 移到 Current。
- [x] requirement 明确说明：本能力完成的是验收流程和证据归档能力，不代表本地已完成真实设备 P0-P6。

## 7. roadmap 回写

- [x] `.codestable/roadmap/cpp-spray-control/cpp-spray-control-items.yaml` 中 `hardware-acceptance` 已从 `in-progress` 改为 `done`。
- [x] `.codestable/roadmap/cpp-spray-control/cpp-spray-control-roadmap.md` 子 feature 清单和变更日志已同步。
- [x] YAML 校验通过。

## 8. attention.md 候选盘点

- [x] 无新增候选。现有 attention 已覆盖 Qt/MinGW PATH、中文路径 build 限制、旧 Python 项目边界和 DUCO 指南位置。

## 9. 遗留

- 后续优化点：真实设备 P0-P6 执行后，若某阶段失败，按具体失败现象另起 issue。
- 已知限制：当前本地验收无法接入实体新松机械臂、相机、PLC，因此不声称现场硬件已通过。
- 顺手发现：Qt HMI 代码已由用户提供并单独提交，本 feature 不把 HMI 代码纳入硬件验收范围。
