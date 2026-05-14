# 嵌入式调试上位机 Pro

面向 ESP32、STM32、GD32 和工控开发者的跨平台调试工作台 MVP。当前实现采用 `Electron + React + TypeScript + Vite`，原因是串口、TCP/UDP、MQTT、日志文件和报告导出都需要桌面端本地能力，同时保留浏览器演示模式，便于快速预览和产品验证。

## 已实现能力

- 串口调试：端口、波特率、数据位、停止位、校验配置；Electron 下通过可选 `serialport` 适配器接入。
- TCP 调试：Electron 主进程使用 Node `net.Socket` 建立连接、发送和接收。
- UDP 调试：Electron 主进程使用 Node `dgram` 绑定本地端口并向远端发送。
- MQTT 调试：Electron 主进程使用 `mqtt` 连接 Broker、订阅和发布 Topic。
- Modbus RTU/TCP：RTU 自动 CRC16，TCP 自动 MBAP，支持 FC03、FC04、FC06、FC16 帧生成和响应解析。
- CAN 数据解析：支持 `18FF50E5#112233...` 和空格分隔格式，提供 little-endian 信号解析和换算。
- 数据图表：从 JSON、文本数字、Modbus 响应中提取电压、电流、温度、转速并实时绘图。
- 日志记录：RX、TX、系统事件、解析结果统一入库，支持搜索、CSV/JSON 导出。
- 自动发送脚本：按行发送，支持通道选择、固定间隔、循环执行。
- 协议模板：内置 ESP32、STM32、GD32、工控 CAN/Modbus 模板命令。
- 报告导出：生成包含统计和日志明细的 HTML 报告。
- 收费方案页：免费基础版、Pro 买断、Pro 订阅、企业授权的产品化承载。

## 运行

```bash
npm install
npm run dev:web
```

浏览器访问：

```text
http://localhost:5173
```

桌面端运行：

```bash
npm run dev
```

如果 Electron 二进制下载超时，可以使用镜像：

```bash
ELECTRON_MIRROR=https://npmmirror.com/mirrors/electron/ npm install
```

`npm run dev` 会在启动 Electron 时自动设置该镜像；Linux 开发模式通过 `--no-sandbox` 启动，避免本地开发环境中 `chrome-sandbox` 未设置 root/setuid 时 Electron 无法启动。

浏览器页面在 Chrome/Edge 的 `localhost` 环境下支持 Web Serial 串口本地模式；浏览器不能直接打开原生 TCP/UDP Socket，本地 TCP/UDP/MQTT 适配请使用 Electron 桌面端。

Linux 串口设备通常属于 `dialout` 组。如果打开 `/dev/ttyUSB0` 提示权限不足，执行以下命令后注销并重新登录：

```bash
sudo usermod -aG dialout $USER
```

## 构建检查

```bash
npm run build
```

当前构建会生成 `dist/`。Vite 可能提示单个 JS chunk 超过 500 kB，这是 Recharts、MQTT 等依赖集中打包导致的体积提示，不影响功能。后续商业化打包时可以按页面做动态加载。

## 平台边界

浏览器模式默认使用演示数据源，适合验证 UI、模板、脚本、图表、日志和报告。

Electron 模式会启用 `window.desktopAPI`：

- TCP、UDP、MQTT 已在 Electron 主进程中实现真实适配。
- 串口通过 `optionalDependencies.serialport` 动态加载；如果本机 native 包不可用，界面仍可用演示模式。
- CAN 当前实现为帧解析和信号换算工具。真实 CAN 设备需要按平台增加 SocketCAN、PCAN、ZLG、Kvaser 或厂商 SDK 适配器。

## 后续产品化建议

- 增加设备配置库：开发板、串口参数、网络端点、MQTT Topic、Modbus register map、CAN signal map。
- 引入插件式协议解析：将私有协议、DBC、JSON schema、Modbus map 做成可导入协议包。
- 增加离线授权与许可证校验：免费基础版、Pro 买断、订阅、企业授权分别控制高级功能。
- 增加自动化测试：协议帧生成、CRC、日志导出和主进程 IPC 需要单元测试；真实硬件适配器需要集成测试。
- 增加打包发布：使用 `electron-builder` 或 `electron-forge` 输出 Windows/macOS/Linux 安装包。
