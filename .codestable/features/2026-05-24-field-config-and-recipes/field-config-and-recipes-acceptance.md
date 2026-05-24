---
doc_type: feature-acceptance
feature: 2026-05-24-field-config-and-recipes
status: accepted
summary: 验收现场 recipe 配置、字段映射、喷枪 IO、DUCO 注入和范围守护
tags: [field-config, recipe, duco, spray]
---

# field-config-and-recipes 验收报告

> 阶段：阶段 3（验收闭环）
> 验收日期：2026-05-24
> 关联方案 doc：`.codestable/features/2026-05-24-field-config-and-recipes/field-config-and-recipes-design.md`

## 1. 接口契约核对

- [x] `RobotConfig::recipePath`：`src/config/AppConfig.h/.cpp` 已新增路径字段，默认 `config/motion_recipes.toml`，`[robot] recipe_path` 可覆盖。
- [x] `MotionRecipeTable::load(path, &error)`：已落地加载入口，返回 table 或 error；`recipeForArm(int)` 和 `recipes()` 支持查询。
- [x] `MotionRecipeTable` 校验：覆盖 arm 1/2 必须存在、重复 arm、`pose_indices` 6 个非负索引、速度/加速度、`spray_io` 和通道。
- [x] 配置示例：`config/motion_recipes.toml` 提供 arm1/arm2 示例，并在文件首行标注生产前必须替换现场参数。
- [x] DUCO 注入：`Application::initialize()` 在 DUCO 模式加载 recipe table，并逐条调用 `DucoRobotController::setMotionRecipe()`。
- [x] 流程图核对：`AppConfig -> MotionRecipeTable -> DucoRobotController -> MotionPlanner` 均有代码落点。

## 2. 行为与决策核对

- [x] 配置表独立于 AppConfig：`AppConfig` 只保存路径，`MotionRecipeTable` 单独解析 `[[recipes]]`。
- [x] robot 层拥有 recipe 语义：recipe table、planner、DUCO controller 都在 `src/robot`；`src/core` 没有 recipe 字段。
- [x] Application 只负责装配：`Application` 不解析字段，只加载 table 并注入 controller。
- [x] 缺失 recipe 是运行期拒绝：`DucoRobotController::enqueueTask()` 在 planner 失败时 warning + `taskFinished(false)`；`DequeueCoordinator` 只在 ok=true 时发 `XD`。
- [x] fake robot 不依赖 recipe 文件：DUCO 分支外不加载 `MotionRecipeTable`，现有 fake application loop 继续通过。
- [x] 默认空任务不依赖 recipe：`DucoRobotController::enqueueTask()` 对 `defaultNoop` 直接 accepted/finished；`MotionPlannerTest::plansDefaultNoopWithoutRecipe` 覆盖。
- [x] 挂载点反向核对：recipe 引用只落在 config、robot、Application、CMake、测试和示例配置。
- [x] 拔除沙盘：移除 recipe 路径、`MotionRecipeTable`、DUCO 注入和测试后，现场 recipe 文件化能力消失；PLC/相机/Modbus 主流程不需要反向改动。

## 3. 验收场景核对

- [x] S1 合法 recipe 文件：`MotionRecipeTableTest::loadsValidArmRecipes` 验证两个 enabled recipe、arm 查询、IO 类型和 qNear。
- [x] S2 重复 arm：`rejectsDuplicateArm` 加载失败并提示 duplicate motion recipe arm。
- [x] S3 字段映射非法：`rejectsInvalidPoseMapping`、`rejectsNegativePoseMapping` 覆盖非 6 个和负数索引。
- [x] S4 速度/IO 非法：`rejectsInvalidSpeed`、`rejectsInvalidSprayIo`、`rejectsInvalidSprayIoChannel` 覆盖加载失败；disabled recipe 由 `MotionPlannerTest::rejectsDisabledRecipe` 覆盖。
- [x] S5 DUCO 装配：`DucoRobotControllerTest::configuredRecipeExecutesPlannedMotionSequence` 验证配置 recipe 可执行最小 motion 序列。
- [x] S6 缺失 recipe：`DucoRobotControllerTest::missingRecipeFailsWithoutAcceptedOrMotion` 验证不 accepted、不调用 motion、finished false。
- [x] S7 默认空任务：`defaultNoopDoesNotCallMotionClient` 验证不调用 DUCO motion/IO，仍成功结束。
- [x] Application 缺失 recipe 文件：`ApplicationLoopTest::ducoModeRejectsMissingRecipeFile` 验证 initialize 失败并返回可观察错误。
- [x] 构建与测试：ASCII 镜像构建通过，18 个 QtTest 组总计 135 passed / 0 failed。

## 4. 术语一致性

- [x] `FieldConfig` 在实现中表现为 `[robot].recipe_path` + `config/motion_recipes.toml` 文件集合。
- [x] `MotionRecipeTable`、`MotionRecipe`、`PoseFieldMapping`、`SprayIoConfig` 与 design 第 0 节命名一致。
- [x] 防冲突：`AppConfig` 没有承载 recipe 数组，`core::RobotTask` 和 `QueueManager` 没有新增 recipe 字段。

## 5. 架构归并

- [x] `.codestable/architecture/ARCHITECTURE.md` 术语表已补 FieldConfig、MotionRecipeTable、PoseFieldMapping、SprayIoConfig。
- [x] `.codestable/architecture/ARCHITECTURE.md` 已新增 `3.7 已落地现场 recipe 配置`，说明路径、table、示例、DUCO 注入和失败边界。
- [x] `.codestable/architecture/ARCHITECTURE.md` 已补硬边界：现场 recipe 参数必须来自配置，不得硬编码真实字段映射、速度、坐标系或喷枪 IO。

## 6. requirement 回写

- [x] `.codestable/requirements/field-config-and-recipes.md` 已从 `draft` 升级为 `current`。
- [x] 已补当前实现，保留能力愿景、用户故事和边界。
- [x] `.codestable/requirements/VISION.md` 已把 `field-config-and-recipes` 从 Draft 移到 Current。

## 7. roadmap 回写

- [x] `cpp-spray-control-items.yaml` 中 `field-config-and-recipes` 已从 `in-progress` 改为 `done`。
- [x] `cpp-spray-control-roadmap.md` 第 5 节已同步标记 done 和 feature 目录。
- [x] roadmap 变更日志已追加 field config 完成记录。

## 8. attention.md 候选盘点

- [x] 无新增候选：中文路径 Qt moc、MinGW PATH 和旧 Python 参考边界已在 attention.md 记录；本 feature 未暴露新的每次必读环境坑。

## 9. 遗留

- 后续优化点：真实现场 payload 字段含义、坐标单位、喷枪 IO 接线和硬件逐点验收进入 `hardware-acceptance`。
- 已知限制：当前 recipe 只覆盖既有最小运动序列 `movej_pose2 -> IO on -> movel -> IO off`；多段路径 DSL 后续另起 feature。
- 顺手发现：`Application.cpp` 继续变胖，后续硬件验收或 HMI 接入后可另走 `cs-refactor` 评估 service factory / `AppContext`。
