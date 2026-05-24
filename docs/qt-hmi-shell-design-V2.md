---
doc_type: feature-design
feature: qt-hmi-shell-v2-detailed
requirement: qt-hmi-shell
roadmap: cpp-spray-control
status: draft
summary: Qt HMI Shell 界面极度详尽设计方案。包含目录结构、文件拆分、类与函数职责、静态资源清单及 CMake 集成。
tags: [qt, hmi, ui, snapshot, detailed-design]
---

# Qt HMI Shell 前端架构详设 (V2 - 详尽版)

## 0. 设计目标

基于 C++ 底层现有的 `core`、`io`、`robot` 模块，全面实现针对工业暗黑风格（Dark Industrial Theme）的 Qt 6 Widgets 界面开发。
本方案将设计**下沉到每个具体的文件、类、成员函数以及必要的静态资源配置**，确保前端开发人员能够直接按照此蓝图进行编码，不再产生架构分歧。

---

## 1. 目录与文件结构总览

为了不污染后端业务代码，所有前端代码均放入 `src/ui/`，静态资源放入项目根目录的 `resources/`。

```text
C:\Data\Code\喷涂开发\
├── resources/                          # 静态资源目录 (New)
│   ├── ui.qrc                          # Qt 资源索引文件
│   ├── styles/
│   │   └── dark_industrial.qss         # 全局暗黑工业风样式表
│   ├── fonts/
│   │   └── Roboto-Regular.ttf          # 界面默认等宽/常规字体
│   └── icons/                          # SVG 图标库
│       ├── start.svg, stop.svg         # 启停按钮图标
│       ├── menu_overview.svg           # 左侧导航：总览
│       ├── menu_queue.svg              # 左侧导航：队列
│       ├── menu_device.svg             # 左侧导航：设备状态
│       ├── menu_log.svg                # 左侧导航：日志
│       ├── menu_config.svg             # 左侧导航：配置
│       ├── menu_simulator.svg          # 左侧导航：联调模拟
│       └── status_led_green.svg, ...   # 状态指示灯 (绿黄红灰)
│
└── src/
    └── ui/                             # 界面核心源码目录 (New)
        ├── CMakeLists.txt              # UI 模块独立构建脚本
        ├── ApplicationHmiRunner.h/.cpp # 衔接无头模式与 GUI 模式的入口
        ├── MainWindow.h/.cpp           # 主窗口容器
        ├── SnapshotProvider.h/.cpp     # 全局快照轮询器
        ├── HmiCommandPort.h/.cpp       # UI 指令单向发送网关
        │
        ├── components/                 # 可复用自定义控件
        │   ├── DeviceCardWidget.h/.cpp # 顶部设备状态卡片
        │   ├── FlowMonitorWidget.h/.cpp# 流程状态监控流向图 (自定义绘制)
        │   ├── LedIndicator.h/.cpp     # 通用圆形状态灯控件
        │   └── TitleBarWidget.h/.cpp   # 停靠面板标题栏
        │
        ├── models/                     # Qt MVC 视图模型 (用于 QTableView)
        │   ├── QueueTableModel.h/.cpp  # 队列表格数据模型
        │   └── EventLogTableModel.h/.cpp # 事件日志表格数据模型
        │
        └── pages/                      # 左侧导航对应的各个独立页面
            ├── OverviewPage.h/.cpp     # 总览页面 (核心看板)
            ├── QueuePage.h/.cpp        # 深度队列浏览页面
            ├── DevicePage.h/.cpp       # 设备详细状态页面
            ├── EventLogPage.h/.cpp     # 日志查询与导出页面
            ├── ConfigPage.h/.cpp       # 现场配置修改页面
            └── SimulatorPage.h/.cpp    # 假信源/联调发包页面
```

---

## 2. 核心基础设施类与函数设计

### 2.1 `src/ui/MainWindow.h / .cpp`
*   **职责**：应用程序的顶级窗口，负责组装顶部工具栏、左侧菜单（`QListWidget` 或按钮组）、中央页面栈（`QStackedWidget`）和底部状态栏。
*   **关键成员变量**：
    *   `QStackedWidget* m_pageStack;` // 承载所有 Page
    *   `SnapshotProvider* m_snapshotProvider;`
    *   `HmiCommandPort* m_commandPort;`
    *   `QLabel* m_lblGlobalStatus;` // 底部状态栏标签
*   **核心函数**：
    *   `void setupUi();` // 初始化整体 Layout，加载 QSS 文件
    *   `void createToolBar();` // 生成顶部 Start/Stop/PLC 等全局操作按钮
    *   `void wireSignalsAndSlots();` // 绑定左侧菜单点击到 switchPage()，绑定按钮到 HmiCommandPort
    *   `void switchPage(int index);` // 切换中央显示的页面
*   **槽函数 (Slots)**：
    *   `void onSystemSnapshotUpdated(const SystemSnapshot& snap);` // 接收快照，仅刷新顶部全局指示灯和底部环境变量栏，其余数据透传给当前显示的 Page。

### 2.2 `src/ui/SnapshotProvider.h / .cpp`
*   **职责**：UI 线程唯一合法的数据来源。内部运行定时器，通过后端 `Application` 读取 `SystemSnapshot`，并通过 Qt 信号多播给整个 UI 层。
*   **关键成员变量**：
    *   `QTimer* m_refreshTimer;`
    *   `std::shared_ptr<Application> m_appCore;` // 弱耦合的底层应用实例
*   **核心函数**：
    *   `void start(int intervalMs = 500);`
    *   `void stop();`
*   **信号 (Signals)**：
    *   `void systemSnapshotUpdated(const SystemSnapshot& snapshot);`

### 2.3 `src/ui/HmiCommandPort.h / .cpp`
*   **职责**：防误触安全网关。所有来自 UI 的写入与控制指令必须经过此类，由该类负责弹窗拦截、二次确认，并在调用底层 API 前后自动写入审计日志。
*   **核心函数**：
    *   `void requestStartSystem(QWidget* parentWindow);` // 请求系统启动
    *   `void requestEmergencyStop(QWidget* parentWindow);` // 弹出危急确认框，确认后调用 `IRobotController::stop()` 并清空缓存。
    *   `void requestClearAlarms();`
    *   `void injectSimulatorPayload(int port, const QByteArray& data);` // 仅在 robot_mode=fake 下允许注入

---

## 3. UI 独立页面类与函数设计 (Pages)

### 3.1 `src/ui/pages/OverviewPage.h / .cpp` (总览看板)
*   **职责**：还原参考图中密度最高的主看板页面。
*   **关键成员变量**：
    *   `std::vector<DeviceCardWidget*> m_deviceCards;` // 顶部的多个设备状态卡
    *   `QTableView* m_queueTableView;`
    *   `QueueTableModel* m_queueModel;`
    *   `FlowMonitorWidget* m_flowMonitor;` // 流程监控图
    *   `QTableView* m_logTableView;` // 底部实时简易日志
*   **核心函数**：
    *   `void setupUi();` // 使用 QSplitter 实现上下、左右的灵活可拖拽比例布局。
    *   `void applySnapshot(const SystemSnapshot& snapshot);` // 槽函数：被 MainWindow 转发。将快照数据拆解后分别调用卡片、表格模型和流程图的更新接口。

### 3.2 `src/ui/pages/ConfigPage.h / .cpp` (配置修改页)
*   **职责**：读取解析 `config/default.toml`，提供表单（`QLineEdit`, `QComboBox`）进行参数覆写。
*   **核心函数**：
    *   `void loadCurrentConfig();` // 打开页面时读取并填充表单
    *   `void validateAndSaveConfig();` // 点击保存时，先进行端口冲突、地址越界校验，然后落地为文件。并弹出 Toast 提示“修改已保存，请重启软件生效”。

### 3.3 `src/ui/pages/SimulatorPage.h / .cpp` (联调模拟页)
*   **职责**：图形化发包工具，用于现场无真实相机或无 PLC 时进行单体功能验证。
*   **核心函数**：
    *   `void onBtnTriggerCameraClicked();` // 构造相机触发心跳包，通过 CommandPort 注入。
    *   `void onBtnMockPlcDequeueClicked();` // 调整假 PLC 指针，强制推进队列。

---

## 4. 自定义控件与数据模型设计 (Components & Models)

### 4.1 `src/ui/components/FlowMonitorWidget.h / .cpp` (核心亮点控件)
*   **职责**：绘制入队与出队生命周期流向图（连线+节点）。
*   **实现细节**：继承 `QWidget`。内部持有一组 `enum Stage { PLC_RCV, CAM_TRIG, CAM_RSP, ENQUEUED }`，重写 `paintEvent(QPaintEvent* e)`。
*   **核心函数**：
    *   `void updateEntryFlow(int latestPointer, Stage currentStage);` // 使用 QPainter 画出实线、虚线，以及已达标节点的绿色 Check 标志。
    *   `void paintEvent(QPaintEvent* event) override;` // 执行原生 2D 绘图。

### 4.2 `src/ui/components/DeviceCardWidget.h / .cpp` (顶部状态卡片)
*   **职责**：统一渲染诸如“PLC 9999”、“相机 2D 在线”的黑底/灰底卡片。
*   **核心函数**：
    *   `void setDeviceName(const QString& name);`
    *   `void setStatus(bool online, const QString& extraText);` // online=true 亮绿灯，false 亮红/黄灯。
    *   `void updateTraffic(const QString& rxTime, const QString& txTime);`

### 4.3 `src/ui/models/QueueTableModel.h / .cpp` (MVC 数据模型)
*   **职责**：将 C++ 端的 `QueueSnapshot` 向量列表转换给 `QTableView`，负责行列映射与状态上色。
*   **核心函数**：
    *   `int rowCount(...) const override;` // 返回队列长度
    *   `int columnCount(...) const override;` // 返回固定列数（Count, Pointer, Command等）
    *   `QVariant data(const QModelIndex &index, int role) const override;`
        *   当 `role == Qt::DisplayRole`：格式化数字和文本（如 `0x2E8F`）。
        *   当 `role == Qt::ForegroundRole`：如果状态是 "Done"，返回绿色的 `QBrush(QColor("#2ECC71"))`；"Pending" 返回蓝色 `QBrush(QColor("#3498DB"))`。
    *   `void applyData(const std::vector<ArmQueueItemSnapshot>& items);` // 替换内部数据并调用 `beginResetModel()` 和 `endResetModel()`。

---

## 5. 静态资源与 QSS 样式表细节

### 5.1 `resources/ui.qrc`
Qt 编译期需要将其打包进二进制。文件内容需确立如下：
```xml
<RCC>
    <qresource prefix="/hmi">
        <file>styles/dark_industrial.qss</file>
        <file>fonts/Roboto-Regular.ttf</file>
        <file>icons/start.svg</file>
        <file>icons/stop.svg</file>
        <file>icons/menu_overview.svg</file>
        <!-- 包含所有提到的 SVG 资产 -->
    </qresource>
</RCC>
```

### 5.2 `resources/styles/dark_industrial.qss` (极简片段)
这是实现 GPT 概念图效果的核心，开发时应遵循如下规范：
```css
/* 全局背景与字体 */
* {
    background-color: #1a1b26; /* 暗色深蓝/藏青底色 */
    color: #a9b1d6;            /* 主要浅色文字 */
    font-family: "Roboto";
    font-size: 14px;
}

/* 顶部与底部状态栏 */
QToolBar, QStatusBar {
    background-color: #16161E;
    border-bottom: 1px solid #292e42;
}

/* 表格组件样式 (QTableView) */
QTableView {
    background-color: #24283b;
    gridline-color: #414868;
    selection-background-color: #3d59a1;
    border: none;
}
QHeaderView::section {
    background-color: #1f2335;
    padding: 4px;
    border: 1px solid #414868;
}

/* 卡片容器样式 (DeviceCardWidget 内部) */
QWidget#CardBackground {
    background-color: #24283b;
    border-radius: 6px;
    border: 1px solid #292e42;
}
```

---

## 6. 与 CMake 骨架的对接集成

为了保证前后端分离以及 headless 模式的兼容，`src/ui` 将作为一个独立的静态库被编译。

### `src/ui/CMakeLists.txt`
```cmake
# 引入 Qt6 组件
find_package(Qt6 COMPONENTS Widgets Gui Core REQUIRED)

# 打包资源文件
qt_add_resources(UI_RESOURCES ../../resources/ui.qrc)

# 将界面生成库
add_library(SprayHmiShell STATIC
    ApplicationHmiRunner.cpp
    MainWindow.cpp
    SnapshotProvider.cpp
    HmiCommandPort.cpp
    components/DeviceCardWidget.cpp
    components/FlowMonitorWidget.cpp
    models/QueueTableModel.cpp
    models/EventLogTableModel.cpp
    pages/OverviewPage.cpp
    # ... 其他 Page ...
    ${UI_RESOURCES}
)

# 依赖核心业务层
target_link_libraries(SprayHmiShell PRIVATE Qt6::Widgets SprayCore)
```

**应用主入口改造 (`src/app/main.cpp`)**：
后端 `main.cpp` 在检查到非 `--headless` 时，调用 `ApplicationHmiRunner::run(argc, argv, coreApp)`。该 Runner 会初始化 `QApplication`（而非 `QCoreApplication`），加载字体与全局 QSS，创建并 `show()` 出 `MainWindow`。
