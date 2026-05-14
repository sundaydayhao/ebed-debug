# 嵌入式调试上位机 Pro

面向 ESP32、STM32、GD32 和工控开发者的 Qt 桌面调试工作台。当前仓库已经重写为 `C++17 + Qt6 + CMake` 方案，只保留桌面端实现，不再包含网页或 Electron 运行路径。

## 当前能力

- 串口调试：端口、波特率、数据位、停止位、校验配置；依赖 `Qt6 SerialPort`。
- TCP 调试：基于 `QTcpSocket` 建立连接、发送和接收。
- UDP 调试：基于 `QUdpSocket` 绑定本地端口并向远端发送。
- MQTT 调试：基于 `Qt6 MQTT` 连接 Broker、订阅和发布 Topic。
- Modbus RTU/TCP：RTU 自动 CRC16，TCP 自动 MBAP，支持 FC03、FC04、FC06、FC16 帧生成和响应解析。
- CAN 数据解析：支持 `18FF50E5#112233...` 和空格分隔格式，提供 little-endian 信号解析和换算。
- 数据图表：从 RX JSON、文本数字和 Modbus 响应中提取电压、电流、温度、转速。
- 日志记录：RX、TX、系统事件、解析结果统一入库，支持搜索、CSV/JSON/HTML 导出。
- 自动发送脚本：按行发送，支持通道选择、固定间隔、循环执行。
- 协议模板：内置 ESP32、STM32、GD32、工控 CAN/Modbus 模板命令。

## 构建

前提：

- `cmake >= 3.21`
- `Qt6 Core / Gui / Widgets / Network`
- 可选：`Qt6 SerialPort`、`Qt6 Charts`、`Qt6 MQTT`

构建命令：

```bash
cmake -S . -B build
cmake --build build
```

运行：

```bash
./build/ebed-debug-pro
```

如果系统里没有完整的 Qt6 开发包，`cmake` 会直接失败。建议优先安装以下模块：

```bash
sudo apt install qt6-base-dev qt6-serialport-dev qt6-charts-dev qt6-mqtt-dev
```

不同发行版包名可能略有差异。

## 模块说明

- `src/MainWindow.*`：Qt Widgets 主界面，包含工作台、图表、脚本、模板、协议、日志、报告页。
- `src/TransportClient.*`：串口、TCP、UDP、MQTT 传输抽象。
- `src/ProtocolUtils.*`：HEX/ASCII、Modbus、CAN、日志导出和报告生成。
- `src/TemplateRepository.*`：内置模板、脚本和 CAN 信号定义。
- `src/Domain.h`：领域模型和通道类型定义。

## 说明

- CAN 当前仍是本地帧解析工具，不直接对接真实 CAN 适配器。
- 如果未安装 `Qt6 SerialPort` 或 `Qt6 MQTT`，程序仍可编译，但对应通道会在运行时提示模块不可用。
- 如果未安装 `Qt6 Charts`，图表页会降级为文本摘要视图。

## 后续建议

- 增加真实 CAN 适配层：SocketCAN、PCAN、ZLG、Kvaser 或厂商 SDK。
- 将设备配置、寄存器映射、DBC/私有协议解析做成可导入协议包。
- 增加单元测试：Modbus 帧生成、CRC、CAN 解码、日志导出。
- 增加安装包发布：`CPack`、`Qt Installer Framework` 或平台原生打包链。
