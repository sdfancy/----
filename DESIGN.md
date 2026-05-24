---
version: alpha
name: 喷涂控制 Qt HMI 设计系统
---

## Overview

工业喷涂控制系统 Qt Widgets HMI 界面设计原型。面向现场操作员和联调人员，提供深色工业风格、高密度信息展示、清晰的设备连接状态和命令操作边界。

- **受众**：操作员、PLC/相机联调人员、机械臂维护人员、软件维护人员
- **方向**：Dense/professional — 深色钢灰工业面板风格
- **页面**：系统总览、队列管理、机械臂控制、PLC 监控、相机状态、诊断日志、系统配置

## Colors

| Token | Value | Usage |
|---|---|---|
| Base bg | `#0f1115` | 页面背景 |
| Surface | `#181c24` | 侧栏背景 |
| Raised | `#1f2430` | 卡片、面板 |
| Ink | `#E8ECF1` | 主要文字 |
| Ink muted | `#8892A0` | 辅助文字 |
| Status OK | `#22c55e` | 正常/连接/运行 |
| Status Warn | `#f59e0b` | 警告/暂停 |
| Status Err | `#ef4444` | 错误/告警/断开 |
| Accent | `#0891b2` | 主色调（青蓝） |

## Typography

- **中文**：PingFang SC, Noto Sans SC, Source Han Sans CN, Microsoft YaHei, system-ui
- **等宽**：ui-monospace, Cascadia Code, monospace（日志、Hex、指针）
- **大标题**：26px / 800 weight
- **KPI 数字**：34px / 800 weight
- **正文**：14px / 400 weight
- **辅助/标签**：11-13px
- **行高**：1.5–1.7

## Rounded

- 卡片/面板：14px
- 按钮：10–12px
- 徽章/筛选：999px (pill)

## Spacing

- 页面内间距：14px grid gap（density 可调）
- 卡片内 padding：16–20px
- 表格单元格：7–10px
- 侧栏宽：230px

## Components

| Component | Variants | Notes |
|---|---|---|
| StatusLED | ok / warn / err / off | 带发光效果的状态指示灯 |
| Badge | ok / warn / err / neutral / accent | 标签/徽章 |
| Icon | 10+ SVG icons | 线条风格 1.8px stroke |
| 命令按钮 | default / danger | 危险命令红色边框+确认弹窗 |
| 数据表 | default | 深色条纹、monospace 数据列 |
| 进度条 | default | 胶囊形，8px 高 |
| KPI 卡片 | default / warn | 大数值展示 |

## Pages

1. **系统总览** — 状态条 + 4 KPI 卡片 + 设备连接 + 流程信息 + 最近事件(5条)
2. **队列管理** — 4 指针概览 + 双臂队列表(8行) + 缓存区 + 3D 周期状态
3. **机械臂控制** — 5 命令按钮 + 双臂状态卡片 + 运动段进度(8段)
4. **PLC 监控** — 4 连接状态卡片 + Modbus 原始命令表(5条)
5. **相机状态** — 3 状态卡片 + 通道详情(3通道)
6. **诊断日志** — 设备/级别过滤器 + 日志表(7条，含错误高亮)
7. **系统配置** — 喷涂参数(5项) + 队列配置(5项) + 运动配方(3个)

## Interaction

- 所有危险命令（停止、急停）触发确认弹窗后才执行
- 侧栏导航选中态使用 inset 左边框 + accent 色高亮图标
- 日志表错误行有红色背景高亮，警告行有黄色背景
- 状态 LED 使用 CSS box-shadow 模拟工业发光效果
