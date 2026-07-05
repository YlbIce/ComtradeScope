<p align="center">
  <img src="./docs/logo.svg" alt="ComtradeScope" width="120" />
</p>

<h1 align="center">ComtradeScope</h1>

<p align="center">
  <strong>Electron + Native C++ COMTRADE 故障录波分析工具</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Electron-39.8-blue?logo=electron" alt="Electron" />
  <img src="https://img.shields.io/badge/C++-20-blue?logo=c%2B%2B" alt="C++20" />
  <img src="https://img.shields.io/badge/platform-Windows-0078D6?logo=windows" alt="Windows" />
  <img src="https://img.shields.io/badge/license-MIT-green" alt="MIT" />
  <img src="https://img.shields.io/badge/COMTRADE-IEEE%20C37.111-orange" alt="COMTRADE" />
</p>

---

## 简介

ComtradeScope 是一款基于 **Electron UI + 独立 Native C++ 后端** 的开源桌面应用，用于电力系统 COMTRADE 故障录波文件的波形查看与分析。前后端通过 **WebSocket + JSON** 协议通信，C++ 后端负责录波文件解析与电气量计算，Electron 前端提供丰富的交互式可视化界面。

> 本项目遵循 IEEE C37.111 (COMTRADE) 标准，支持 ASCII、BINARY、BINARY32、FLOAT32 数据格式。

## 功能

| 模块 | 功能 |
|---|---|
| **波形分析** | 多通道模拟量叠加显示、通道显隐切换、鼠标游标测量、滚轮缩放、数字量逻辑通道叠加 |
| **通道统计** | 模拟通道 Min / Max / RMS / 末周期基波幅值与相角、2 / 3 次谐波幅值近似 THD |
| **数字事件** | 开关量通道变位检测、上升/下降沿列表、CSV 导出 |
| **相量与序分量** | 末周期基波相量图、A/B/C 三相正序 / 负序 / 零序计算 |
| **报告导出** | Markdown 格式分析摘要报告，可直接保存 |
| **示例数据** | 内置 `samples/demo_fault.cfg` + `samples/demo_fault.dat` 示例录波 |

## 架构

```
┌─────────────────────────────────────────────────────┐
│                   Electron Renderer                  │
│        HTML5 Canvas 波形 / 统计 / 相量 / 报告         │
│              contextBridge (安全隔离)                 │
└─────────────────────────┬───────────────────────────┘
                          │ IPC (ipcMain / ipcRenderer)
┌─────────────────────────┴───────────────────────────┐
│                   Electron Main                      │
│     进程管理 · 文件对话框 · 应用生命周期               │
│           WebSocket 客户端 (127.0.0.1:48010)          │
└─────────────────────────┬───────────────────────────┘
                          │ WebSocket + JSON
┌─────────────────────────┴───────────────────────────┐
│                Native C++ Backend                    │
│   Boost.Beast WebSocket 服务器 · 单线程异步 I/O       │
│  ┌─────────────┬───────────────┬──────────────────┐  │
│  │ COMTRADE    │  电气量计算    │  JSON 协议路由    │  │
│  │ 解析器      │  DFT / RMS    │  请求-响应匹配     │  │
│  │ CFG + DAT  │  相量 / 序分量 │  requestId 超时   │  │
│  └─────────────┴───────────────┴──────────────────┘  │
└─────────────────────────────────────────────────────┘
```

## 技术栈

| 层级 | 技术 |
|---|---|
| **前端框架** | Electron 39 + 原生 HTML / CSS / JavaScript |
| **图形渲染** | HTML5 Canvas 2D（波形、相量、数字量时序图） |
| **进程通信** | IPC (`contextBridge` + `ipcMain.handle`) |
| **架构安全** | `contextIsolation: true`、`nodeIntegration: false`、CSP 策略 |
| **后端语言** | C++20 |
| **网络库** | Boost.Beast (WebSocket) + Boost.Asio |
| **JSON** | nlohmann/json |
| **构建系统** | CMake + Ninja + vcpkg |
| **打包工具** | electron-builder → NSIS 安装包 + Portable 便携版 |

## 目录结构

```
ComtradeScope-BoostElectron/
├── src/
│   ├── main/
│   │   ├── main.js          # Electron 主进程入口
│   │   └── preload.js       # 预加载脚本 (contextBridge)
│   └── renderer/
│       ├── index.html        # 渲染进程 UI
│       ├── renderer.js       # 渲染逻辑 (WebSocket / Canvas)
│       └── styles.css        # 暗色主题样式
├── backend/                  # Native C++ 后端
│   ├── CMakeLists.txt        # CMake 构建配置
│   └── src/
│       ├── main.cpp          # 入口 (解析 --port 参数)
│       ├── ComtradeParser.cpp/.h    # CFG + DAT 解析器
│       ├── ComtradeAnalyzer.cpp/.h  # DFT / RMS / 相量 / 序分量
│       ├── ComtradeModel.h          # 数据模型定义
│       ├── WebSocketServer.cpp/.h   # Boost.Beast WebSocket
│       └── ProtocolUtils.cpp/.h     # JSON 消息 / 工具函数
├── samples/
│   ├── demo_fault.cfg        # 示例 COMTRADE 配置文件
│   └── demo_fault.dat        # 示例 COMTRADE 数据文件
├── scripts/
│   ├── build-backend.ps1     # C++ 后端编译脚本
│   └── run-electron.js       # Electron 启动脚本
└── package.json              # npm 配置 + electron-builder 打包配置
```

## 快速开始

### 环境要求

| 工具 | 说明 |
|---|---|
| **Windows 10+** | 仅支持 Windows 平台 |
| **Visual Studio 2022** | 含 MSVC C++ 工具链 |
| **CMake** | ≥ 3.24 |
| **PowerShell 7** | 用于后端构建脚本 |
| **vcpkg** | 默认路径 `..\tools\vcpkg`，需安装以下包 |
| **Node.js** | ≥ 18 |

### vcpkg 依赖安装

```powershell
vcpkg install boost-beast:x64-windows boost-system:x64-windows nlohmann-json:x64-windows
```

### 安装 Node.js 依赖

```powershell
cd ComtradeScope-BoostElectron
npm install
```

### 编译 C++ 后端

```powershell
npm run build:backend
```

构建脚本会自动：
1. 检测 VS 2022 并导入 MSVC 编译环境
2. 使用 CMake + Ninja 编译 C++ 后端
3. 将可执行文件及依赖 DLL 复制到 `backend/bin/`

如果 vcpkg 不在默认路径，可通过参数指定：

```powershell
pwsh -File scripts/build-backend.ps1 -VcpkgRoot "D:\vcpkg"
```

### 启动应用

```powershell
# 开发模式（打开 DevTools）
npm run dev

# 生产模式
npm run start
```

## 发布打包

```powershell
# 完整发布流程：编译后端 → 语法检查 → electron-builder 打包
npm run dist
```

打包产物输出到 `release/` 目录：

| 文件 | 说明 |
|---|---|
| `ComtradeScope-Setup-0.1.0-x64.exe` | NSIS 安装包（支持自定义路径、桌面快捷方式） |
| `ComtradeScope-Portable-0.1.0-x64.exe` | 便携版（免安装直接运行） |

打包配置（`package.json` → `build` 字段）：
- **extraResources**: 自动包含 `backend/bin/`（C++ 后端）和 `samples/`（示例文件）
- **asar**: 前端代码打包为 `app.asar`
- 安装包不支持一键安装（`oneClick: false`），用户可自定义安装路径

## WebSocket 协议

前后端通过 JSON 消息通信，WebSocket 默认端口 `48010`。

### 请求格式

```json
{
  "requestId": "uuid",
  "type": "comtrade:load",
  "payload": {
    "cfgPath": "D:\\data\\fault.cfg",
    "datPath": "D:\\data\\fault.dat"
  }
}
```

### 响应格式

```json
{
  "type": "comtrade:load",
  "requestId": "uuid",
  "payload": { ... }
}
```

### 支持的命令

| Type | 说明 |
|---|---|
| `backend:info` | 获取后端信息 |
| `comtrade:load` | 加载 COMTRADE 录波文件 |
| `backend:shutdown` | 关闭后端服务 |

> 请求包含 `requestId`，响应原样返回以支持请求-响应匹配，默认超时 30 秒。

## COMTRADE 支持矩阵

| 格式 | 状态 |
|---|---|
| **ASCII** | ✅ 完整支持 |
| **BINARY** | ✅ 完整支持 |
| **BINARY32** | ✅ 完整支持 |
| **FLOAT32** | ✅ 完整支持 |
| `.CFG` + `.DAT` 双文件 | ✅ 完整支持 |
| `.CFF` 单文件 | ❌ 待支持 |
| 模拟量 `a*x + b` 缩放 | ✅ 完整支持 |
| 数字量 16-bit word 解包 | ✅ 完整支持 |
| 时间戳倍乘因子 | ⚠️ 部分支持（固定微秒单位） |
| 分段采样率 | ❌ 待支持 |
| 厂商私有字段 | ❌ 待支持 |

## 路线图

- [ ] 添加 `.CFF` 单文件格式支持
- [ ] 时间戳倍乘因子、时区、本地时间格式兼容
- [ ] 厂商私有字段容错解析
- [ ] 分段采样率严格时间轴计算
- [ ] 差动分析 / 阻抗计算 / 故障测距
- [ ] 频率跟踪与全量谐波谱分析
- [ ] 大文件分页/流式读取（后端分页 + 前端按窗口请求）
- [ ] 多语言支持 (i18n)
- [ ] Linux / macOS 跨平台支持
- [ ] 自定义应用图标

## 安全设计

- **进程隔离**: `contextIsolation: true`，渲染进程无法直接访问 Node.js API
- **节点集成禁用**: `nodeIntegration: false`
- **CSP 策略**: `connect-src` 仅允许 `ws://127.0.0.1:48010`
- **预加载沙箱**: 仅通过 `contextBridge` 暴露最小化 API
- **后端进程**: C++ 后端以独立子进程运行，通过 `windowsHide` 隐藏窗口

## 许可证

[MIT License](./LICENSE)

---

<p align="center">
  <sub>Built with Electron ⚡ Boost.Beast ⚡ nlohmann/json</sub>
</p>
