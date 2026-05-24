---
doc_type: requirement
slug: hardware-acceptance
pitch: 把真实硬件联调变成有步骤、有证据、可回退的验收流程
status: current
last_reviewed: 2026-05-24
implemented_by: [2026-05-24-hardware-acceptance]
tags: [hardware, acceptance, plc, camera, duco]
---

# 真实硬件联调验收能力

## 用户故事

- 作为现场联调人员，我希望按固定步骤验证 PLC、相机、机械臂和日志链路，而不是靠临场记忆逐项试。
- 作为设备负责人，我希望每一步都有可保存的证据和通过标准，而不是只听“刚才能跑”。
- 作为操作员，我希望危险动作前有明确前置条件和停止方式，而不是脚本直接驱动机械臂运动。
- 作为维护人员，我希望验收失败后能定位停在哪个环节，并能回退到上一阶段继续排查。

## 为什么需要

C++ 主控的 PLC、相机、DUCO、recipe 和 diagnostics 基础已经具备，但真实现场验收仍然涉及电气接线、网络、SDK、相机数据、喷枪 IO 和安全停止。没有固定流程时，问题容易混在一起，出现机械臂不执行、PLC 没收到反馈或相机数据未入队时很难判断断点。

## 怎么解决

提供一套按阶段推进的现场验收流程：先确认配置、日志和离线 dry-run，再逐步接入 PLC、相机、DUCO 准备流程、低速空喷动作和生产前观察。每一步都写清前置条件、操作命令、预期现象、失败处理和需要保存的证据。

当前仓库已提供 runbook、checklist 和 evidence helper。该能力的完成标准是现场人员可按流程执行并归档证据；真实设备 P0-P6 的通过/失败结果由现场 checklist 和 EvidenceBundle 记录，不在仓库默认状态中伪造成已通过。

## 边界

- 不代替现场安全制度，不自动执行危险运动。
- 不修改 PLC `9999/9090`、相机 legacy/dual camera 或 DUCO motion API。
- 不在仓库中写入真实现场 IP、Smart200 地址、喷枪通道或坐标参数。
- 不实现 Qt HMI 页面；HMI 只消费后续已验收的日志、状态和命令边界。
- 真实验收必须由现场人员确认急停、限位、气源、喷枪和机械臂工作区安全。

## 变更日志

- 2026-05-24：由 `2026-05-24-hardware-acceptance` 落地 runbook、checklist 和只收集证据的 helper，状态升级为 current；现场真实执行结果仍待现场填写。
