---
doc_type: requirement
slug: field-config-and-recipes
pitch: 把现场点位字段、喷涂参数和喷枪 IO 变成可检查的配置
status: current
created: 2026-05-24
last_reviewed: 2026-05-24
tags: [field-config, recipe, duco, spray]
---

# 现场配置与喷涂 recipe 能力

## 1. 能力愿景

新 C++ 喷涂控制项目需要把现场相机 payload 字段、机械臂工具/工件坐标、速度、加速度、喷枪 IO 和 arm1/arm2 差异统一放到配置里。现场人员应能在不改代码的情况下替换点位字段映射和喷涂参数，并在参数缺失或非法时得到明确告警。

## 2. 用户故事

- 作为现场调试人员，我可以按 arm 配置相机 payload 中哪些字段代表位姿，而不是让软件固定读取前 6 个数值。
- 作为工艺人员，我可以调整 approach/line 速度、加速度、圆滑半径、工具坐标和工件坐标，而不重新编译程序。
- 作为电气人员，我可以配置喷枪使用机械臂末端 IO 还是控制柜 IO，以及具体通道号。
- 作为维护人员，我可以在 recipe 缺失、字段越界、速度非法或 IO 配置错误时看到明确错误，而不是机械臂静默不执行。

## 3. 边界

- 不在代码中硬编码现场真实点位、速度、喷枪 IO 或 Smart200 地址。
- 不改变 PLC `9999/9090`、相机 legacy/dual camera 或 DUCO motion API 语义。
- 不实现 Qt 配置编辑页面；本能力只提供配置文件、加载校验和执行链路接入。
- 不做真实硬件参数验收；现场逐点验证留给 `hardware-acceptance`。
- 不自动生成喷涂路径；本阶段仍按相机 payload 字段映射生成现有最小运动序列。

## 4. 验收依据

- 配置文件能表达 arm1/arm2 的工具、工件、qNear、字段映射、速度、加速度、半径和喷枪 IO。
- 配置加载能拒绝缺失 arm、重复 arm、字段映射非 6 个索引、负数索引、非法速度和非法 IO。
- DUCO controller 能从配置获得 arm recipe；缺失或非法 recipe 不执行非默认任务，不发送正常完成 `XD`。
- 默认空任务仍可安全完成，不依赖现场 recipe。

## 5. 当前实现

- `[robot].recipe_path` 默认指向 `config/motion_recipes.toml`，可在现场配置中替换。
- `MotionRecipeTable` 负责加载 `[[recipes]]`、校验 arm1/arm2 和按 arm 查询 `MotionRecipe`。
- DUCO 模式初始化时加载 recipe table 并注入 `DucoRobotController`；加载失败直接拒绝启动。
- fake robot 和默认空任务不依赖 recipe 文件，PLC/相机外部协议不变。

## 6. 变更日志

- 2026-05-24：起草 draft，关联 roadmap item `field-config-and-recipes`。
- 2026-05-24：升级为 current，落地 recipe 文件、加载校验、DUCO 注入和测试覆盖。
