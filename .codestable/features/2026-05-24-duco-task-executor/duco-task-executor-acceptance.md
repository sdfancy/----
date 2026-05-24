---
doc_type: feature-acceptance
feature: 2026-05-24-duco-task-executor
status: passed
accepted_at: 2026-05-24
tags: [duco, robot, motion, queue]
---

# duco-task-executor 验收报告

> 阶段：阶段 3（验收闭环）
> 验收日期：2026-05-24
> 关联方案 doc：`.codestable/features/2026-05-24-duco-task-executor/duco-task-executor-design.md`

## 1. 接口契约核对

**接口示例逐项核对**
- [x] `MotionPlanner::plan(task, recipe)`：输入 `(1001,123,0.49,0.14,0.44,-1.14,0,-1.57)E` → 输出 `MoveJPose2 -> IO on -> MoveL -> IO off`。代码实际：`tests/test_motion_planner.cpp::plansExpectedSpraySequence` 覆盖。

**名词层“现状 → 变化”逐项核对**
- [x] `core::RobotTask` 保留 `armId/count/pointer/payload/defaultNoop`，运动细节未暴露到 core。
- [x] `ArmPayload`、`Pose6d`、`Joint6d`、`MotionSegment`、`MotionRecipe`、`PlannedRobotTask` 已落在 `src/robot/MotionTypes.h`。
- [x] `MotionPlanner` 已解析默认、合法、坏格式、坏 arm、非数值、count mismatch、disabled recipe。
- [x] `IDucoClient` 已新增 `moveJPose2`、`moveL`、`setToolDigitalOut`、`setStandardDigitalOut`。

**流程图核对**
- [x] `PLC -> DequeueCoordinator -> DucoRobotController -> MotionPlanner -> DucoMotionWorker -> IDucoClient -> taskFinished` 均有代码落点。

## 2. 行为与决策核对

**需求摘要逐项验证**
- [x] 非默认出队任务从 raw payload 规划为 DUCO motion/IO 段，证据：`validTaskExecutesPlannedMotionSequence`。
- [x] 默认空任务仍 accepted/finished，证据：`defaultNoopDoesNotCallMotionClient`。
- [x] 失败任务不会伪装完成，证据：`failedRobotTaskDoesNotSendDoneFeedback` 与 `motionFailureClosesSprayIoAndFaults`。

**明确不做逐项核对**
- [x] 未修改 PLC `9999/9090` 和相机 legacy/dual camera 外部报文。
- [x] grep 未发现旧机械臂 `"data"` 或 `1,count` 协议进入 `src/tests/CMakeLists.txt`。
- [x] grep 未发现 `QModbus`、`QtWidgets` 新增。
- [x] 默认构建仍不强制链接真实 DUCO SDK。

**关键决策落地**
- [x] D1：core 不携带 DUCO 细节 → motion types 和 planner 在 robot 层。
- [x] D2：失败不发正常 `XD` → `DequeueCoordinator::onTaskFinished(ok=false)` 直接返回。
- [x] D3：accepted 表示开始执行 → DUCO 任务规划成功后进入 queued worker 才 emit accepted。
- [x] D4：recipe 缺失即拒绝 → `missingRecipeFailsWithoutAcceptedOrMotion` 覆盖。
- [x] D5：DUCO API 扩展走 seam → recording client 覆盖 motion/IO 调用。

**挂载点反向核对**
- [x] `IDucoClient` motion/IO seam、`MotionPlanner`、`DucoRobotController` 执行队列、`DequeueCoordinator` 完成语义、recipe 注入入口均已落地。
- [x] 反向 grep 显示本 feature 新引用集中在 `src/robot`、`src/core/DequeueCoordinator.cpp`、测试和 CMake，符合挂载点清单。
- [x] 拔除沙盘：移除 motion types/planner/worker/seam 扩展和 controller 调度后，DUCO 任务执行能力消失；core/PLC/相机仍可保持旧闭环。

## 3. 验收场景核对

- [x] S1 默认空任务：`defaultNoopDoesNotCallMotionClient` + fake loop 测试通过。
- [x] S2 有效 payload + recipe：`validTaskExecutesPlannedMotionSequence` 验证 `movej_pose2 -> IO on -> movel -> IO off`。
- [x] S3 无效 payload：`test_motion_planner` 覆盖坏格式、坏 arm、非数值、count mismatch。
- [x] S4 recipe 缺失：`missingRecipeFailsWithoutAcceptedOrMotion` 验证不 accepted、不调用 motion。
- [x] S5 DUCO motion 返回 `-1`：`motionFailureClosesSprayIoAndFaults` 验证关闭喷枪 IO、faulted、不成功完成。
- [x] S6 stop/pause/resume：既有 `taskControlUsesControlClient` 仍通过，未混用 motion client。
- [x] S7 PLC/相机流程守护：`spray_tests.exe` 全量通过。

## 4. 术语一致性

- `ArmPayload`、`MotionSegment`、`MotionRecipe`、`Pose6d`、`Joint6d`、`PlannedRobotTask`、`DucoMotionWorker` 代码命名与方案一致。
- 禁用词 grep：旧 `"data"` 请求、`1,count` 完成协议、`QModbus`、`QtWidgets` 在本次范围无命中。

## 5. 架构归并

- [x] `.codestable/architecture/ARCHITECTURE.md`：补入新增术语和已落地 DUCO 任务执行基础。
- [x] `.codestable/architecture/ARCHITECTURE.md`：补入 `taskFinished(ok=false)` 不发送正常 `XD` 的队列反馈语义。
- [x] `.codestable/architecture/ARCHITECTURE.md`：补入 recipe 缺失拒绝执行、后续完整 recipe 管理仍未完成的约束。

## 6. requirement 回写

- [x] `.codestable/requirements/duco-task-executor.md` 从 `draft` 升级为 `current`。
- [x] 保留愿景，补充已落地最小 `MotionRecipe` 注入边界和 2026-05-24 变更日志。

## 7. roadmap 回写

- [x] `.codestable/roadmap/cpp-spray-control/cpp-spray-control-items.yaml` 中 `duco-task-executor` 从 `in-progress` 改为 `done`。
- [x] `.codestable/roadmap/cpp-spray-control/cpp-spray-control-roadmap.md` 子 feature 清单和变更日志同步为 done。
- [x] roadmap items yaml 校验通过。

## 8. attention.md 候选盘点

- [x] 本 feature 未暴露新的 attention.md 候选。现有 ASCII 构建目录和 Qt PATH 注意事项仍适用。

## 9. 遗留

- 已知限制：完整现场 recipe 文件结构、真实相机字段映射、喷枪 IO 接线、Qt HMI、Modbus 和真实硬件验收仍由后续 roadmap item 处理。
- 顺手发现：`Application.cpp` 已继续偏胖，后续 HMI/Modbus 接入前建议单独走 `cs-refactor` 抽 service factory；本 feature 未处理。
