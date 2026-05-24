---
schemaVersion: 1
scope: workspace
updatedAt: "2026-05-24T11:54:55.156Z"
workspaceName: "spray_control"
---

# Project Memory

## Project Overview
- C++ 喷涂控制系统，从 Python 队列管理项目重写，保持 PLC/相机协议不变。
- 整合新松 DUCO 机械臂二次开发 API，通过 Qt Widgets 提供工业 HMI。
- 工作区包含旧版参考（队列管理/）、架构文档（.codestable/architecture/）、需求定义（.codestable/requirements/）和 Qt HMI 详细设计（docs/qt-hmi-shell-design-V2.md）。

## Current State
- 已完成需求分析（qt-hmi-shell 等）、架构梳理和现有 Web Dashboard（dashboard.html）研究。
- 已产出新版 Qt 界面可视化原型（App.jsx）和配套设计系统（DESIGN.md），原型覆盖总览、队列、机械臂控制等 7 个页面。
- 原型基于深色工业风（Dark Industrial Theme），使用领域真实模拟数据，零预览错误。

## Artifacts
- `队列管理/src/web/static/dashboard.html`：旧 Python 项目 Web 监控面板，作为交互模式参考。
- `docs/qt-hmi-shell-design-V2.md`：Qt HMI 详细设计蓝图，包含文件结构、类职责、资源规划等。
- `App.jsx`：新版界面 React 原型，演示导航、数据表、状态徽章、命令按钮和 3D 追踪等组件。
- `DESIGN.md`：从原型提炼的设计系统文档，定义色彩、字体、表面层级等基础样式。

## Design Direction
- 工业暗黑主题，高对比度，侧边栏导航，信息密度高但可读性优先。
- 使用彩色状态徽章（OK/警告/错误）、分区卡片、危险命令需二次确认。
- 设计语言遵循现有需求文档中的“只读快照+高层命令”模型，不直接修改队列数据。

## User Feedback
- 暂无。

## Decisions
- 采用 React 临时原型来快速可视化界面，但不替代最终 Qt 实现。
- 导航结构基于需求中的核心功能：系统总览、主队列、双臂出队、历史日志、系统诊断、机械臂控制、服务启动。
- 界面组件使用表面层级系统（base/raised/overlay），颜色取自深色工业调色板。

## Open Questions
- 最终 Qt 实现时是否需要调整布局以适配不同屏幕分辨率（如 1920x1080 固定或响应式）？
- 历史日志表格是否需分组筛选或搜索功能？
- 机械臂控制页的 3D 轨迹段可视化在 Qt 中采用什么技术栈（3D 引擎 vs 矢量图）？

## Next Steps
- 将原型关键页面结构映射到 Qt 蓝图中的类/文件。
- 根据原型反馈细化 DESIGN.md 中组件规范（按钮、表格、状态灯等）。
- 启动 Qt 前端编码，按页面逐步实现。

## Promotion Candidates For DESIGN.md
- 状态徽章颜色体系（ok, warn, err, neutral, accent）已稳定，可直接移入 DESIGN.md。
- 表面层级与间距（density 因子）已验证，建议作为系统规范。

## Recent History
- 2026-05-24: 分析项目架构、需求、现有设计文档和 Web Dashboard，创建深色工业风原型及 DESIGN.md。