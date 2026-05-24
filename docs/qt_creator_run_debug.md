---
doc_type: guide
status: current
last_reviewed: 2026-05-24
tags: [qt, debug, hmi]
---

# Qt Creator 运行调试

## 目标

让 `spray_control` 可以直接在 Qt Creator 中运行和断点调试 HMI，不需要先手工打包 exe。

## 推荐配置

1. 用 Qt Creator 打开仓库根目录的 `CMakeLists.txt`。
2. 选择 CMake preset：`Qt 6.10 MinGW Debug`。
3. 确认 build directory 为 `C:/Data/Code/spray_control_build_qtcreator_debug`。
4. Build target 选择 `spray_hmi`。
5. Run configuration 保持默认参数即可，不要加 `--headless`。
6. 点击绿色运行按钮启动 HMI，点击调试按钮可断点调试。

## 当前默认行为

- CMake 每次构建 `spray_hmi`、`spray_control` 或 `spray_tests` 后，会把仓库 `config/` 复制到 exe 同目录。
- CMake 会在 exe 同目录创建 `logs/`。
- 默认配置使用 fake robot，适合先做界面和本地协议调试。
- 如果要在 Qt Creator 中调试真实设备，把 build 目录下 exe 同级的 `config/default.toml` 和 `config/motion_recipes.toml` 替换为现场确认值。

## 注意

- 项目源码路径包含中文时，不要把 build 目录放在项目内，否则 Qt `moc` 可能失败。
- `spray_hmi` 是 Qt Creator 调试界面的专用目标；即使 Run arguments 里误留了 `--help`，也不会弹 usage 帮助框。
- `spray_control` 保留命令行入口，不带 `--headless` 时也能启动 Qt Widgets HMI。
- `spray_tests` 用于单元测试，不作为 HMI 启动目标。
