# ComtradeScope Native Electron

故障录波仪 COMTRADE 波形显示软件 Demo。架构沿用“Electron UI + 独立 Native C++ 后端 + WebSocket + JSON 消息”的桌面客户端模式。

## 功能

- 打开 COMTRADE `.CFG + .DAT` 文件，自动识别同名 DAT。
- C++ 后端解析 ASCII、BINARY、BINARY32、FLOAT32 的基础 DAT 数据。
- 波形浏览：模拟量多通道叠加、通道显隐、鼠标游标、滚轮缩放、数字量叠加。
- 通道统计：Min、Max、RMS、末周期基波 RMS、相角、2/3 次谐波近似 THD。
- 数字事件：开关量变位列表、上升沿/下降沿、CSV 导出。
- 相量与序分量：末周期基波相量、A/B/C 三相正序/负序/零序计算。
- 报告导出：Markdown 摘要报告。
- 内置示例录波：`samples/demo_fault.cfg` + `samples/demo_fault.dat`。

## 架构

```text
Electron Renderer
  - Canvas 波形绘图
  - 通道选择 / 游标 / 报告
        |
        | WebSocket JSON
        v
Electron Main
  - 启动 C++ 后端
  - 文件选择 / 保存对话框
        |
        v
Native C++ Backend
  - Boost.Beast WebSocket
  - COMTRADE CFG/DAT 解析
  - RMS / 相量 / 序分量 / 数字事件分析
```

## 环境

- Windows
- Visual Studio 2022 MSVC
- CMake
- PowerShell 7
- vcpkg：默认查找 `..\tools\vcpkg`
- vcpkg 包：`boost-beast`、`boost-system`、`nlohmann-json`
- Electron：可复用上层 `D:\WORKSPACE\Electron\node_modules`

## 构建与运行

```powershell
npm run build:backend
npm run check
npm run start
```

如果后端没有自动启动，确认：

```text
backend/bin/comtradescope-backend.exe
```

存在。

## COMTRADE 支持边界

当前是工程 Demo 级实现：

- 已支持 `.CFG + .DAT`。
- 已支持 ASCII、BINARY、BINARY32、FLOAT32 基础读取。
- 二进制时间戳按常见微秒单位转换为秒。
- 已处理模拟量 `a*x+b` 缩放。
- 数字量按 16 bit word 解包。

后续生产化建议：

- 增加 `.CFF` 单文件支持。
- 增加时间戳倍乘因子、时区、本地时间格式兼容。
- 增加厂商私有字段容错。
- 增加分段采样率严格时间轴计算。
- 增加差动/阻抗/故障测距、频率跟踪、全量谐波谱。
- 大文件改为后端分页/流式读取，前端按窗口请求数据。

## 参考

COMTRADE 是电力系统暂态数据交换的通用格式，核心文件通常由 `.CFG` 配置文件和 `.DAT` 数据文件组成；IEEE C37.111-2013 引入了更多数据格式和时区等字段。商业/工程录波分析软件常见能力包括通道波形查看、游标测量、RMS/相量/谐波、数字量事件和分析报告，本 Demo 按这些能力组织界面和后端职责。
