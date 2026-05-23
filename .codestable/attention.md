# Attention

本文件是 CodeStable 技能启动必读的项目注意事项入口。所有 CodeStable 子技能开始工作前必须读取它。

## 项目碎片知识

<!-- cs-note managed: 用 cs-note 维护，新条目按下面分节追加 -->

### 编译与构建

- Windows/Qt 6 MinGW 构建需要把 `C:\Qt\Tools\mingw1310_64\bin` 和 `C:\Qt\6.10.2\mingw_64\bin` 放进 PATH；项目源码路径含中文时，Qt `moc` 在项目内 `build/` 下会失败，建议使用 ASCII 构建目录，如 `C:\Data\Code\spray_control_build_mingw`。

### 运行与本地起服务

### 测试

### 命令与脚本陷阱

### 路径与目录约定

- 根目录 `队列管理/` 是旧 Python 队列管理项目，作为协议和流程参考，不直接作为新 C++ 项目的代码边界。
- 根目录 `MinerU_markdown_duco-develop-v4.3-zh_2058173104099405824.md` 是新松机械臂二次开发指南，机器人控制方案以其远程控制 API 为依据。

### 环境变量与凭证

### 其他
