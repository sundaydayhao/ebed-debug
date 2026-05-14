import type { AutoScript, CanSignal, ProtocolTemplate } from "../types";

export const protocolTemplates: ProtocolTemplate[] = [
  {
    id: "esp32-at",
    name: "ESP32 AT/WiFi 快测",
    target: "ESP32",
    description: "串口 AT 指令、WiFi 状态查询和 MQTT 示例载荷。",
    commands: [
      {
        id: "esp32-at",
        name: "AT 握手",
        channel: "serial",
        payload: "AT\r\n",
        format: "text",
        description: "验证 ESP-AT 固件串口响应。"
      },
      {
        id: "esp32-wifi",
        name: "查询 WiFi",
        channel: "serial",
        payload: "AT+CWJAP?\r\n",
        format: "text",
        description: "读取当前接入点信息。"
      },
      {
        id: "esp32-json",
        name: "Telemetry JSON",
        channel: "mqtt",
        payload: "{\"voltage\":3.31,\"current\":0.46,\"temperature\":36.5,\"rpm\":1320}",
        format: "json",
        description: "模拟设备上报数据，用于图表解析。"
      }
    ]
  },
  {
    id: "stm32-boot",
    name: "STM32 Bootloader",
    target: "STM32",
    description: "常见 Bootloader 同步、读 ID 和擦除命令模板。",
    commands: [
      {
        id: "stm32-sync",
        name: "同步 0x7F",
        channel: "serial",
        payload: "7F",
        format: "hex",
        description: "进入系统 Bootloader 后发送同步字节。"
      },
      {
        id: "stm32-get-id",
        name: "读取芯片 ID",
        channel: "serial",
        payload: "02 FD",
        format: "hex",
        description: "STM32 Bootloader Get ID 命令。"
      },
      {
        id: "stm32-ping",
        name: "板级 Ping",
        channel: "tcp",
        payload: "{\"cmd\":\"ping\",\"seq\":1}",
        format: "json",
        description: "用于运行在以太网/串口网关上的自定义固件。"
      }
    ]
  },
  {
    id: "gd32-field",
    name: "GD32 现场采集",
    target: "GD32",
    description: "串口日志、Modbus 寄存器和 UDP 遥测组合。",
    commands: [
      {
        id: "gd32-status",
        name: "STATUS?",
        channel: "serial",
        payload: "STATUS?\r\n",
        format: "text",
        description: "查询固件状态机。"
      },
      {
        id: "gd32-holding",
        name: "读保持寄存器",
        channel: "modbus",
        payload: "01 03 00 00 00 08 44 0C",
        format: "hex",
        description: "读取 0x0000 起始的 8 个 holding registers。"
      },
      {
        id: "gd32-udp",
        name: "UDP 校准",
        channel: "udp",
        payload: "{\"cmd\":\"calibrate\",\"axis\":\"x\"}",
        format: "json",
        description: "向现场网关发送校准指令。"
      }
    ]
  },
  {
    id: "industrial-can",
    name: "工控 CAN/Modbus",
    target: "工控",
    description: "CAN 帧解析、Modbus TCP 和控制指令。",
    commands: [
      {
        id: "can-motor",
        name: "电机状态帧",
        channel: "can",
        payload: "18FF50E5#211F245804000001",
        format: "text",
        description: "J1939 风格的 29-bit 扩展帧样例。"
      },
      {
        id: "modbus-tcp",
        name: "Modbus TCP 读输入",
        channel: "modbus",
        payload: "00 01 00 00 00 06 01 04 00 00 00 04",
        format: "hex",
        description: "MBAP + FC04 读取输入寄存器。"
      },
      {
        id: "tcp-stop",
        name: "急停命令",
        channel: "tcp",
        payload: "{\"cmd\":\"stop\",\"reason\":\"operator\"}",
        format: "json",
        description: "自定义 TCP 控制协议示例。"
      }
    ]
  }
];

export const starterScripts: AutoScript[] = [
  {
    id: "heartbeat",
    name: "设备心跳",
    channel: "serial",
    intervalMs: 1000,
    loop: true,
    lines: "PING\r\n\nSTATUS?\r\n"
  },
  {
    id: "mqtt-telemetry",
    name: "MQTT 遥测压测",
    channel: "mqtt",
    intervalMs: 750,
    loop: true,
    lines: "{\"voltage\":3.32,\"current\":0.41,\"temperature\":35.7,\"rpm\":1280}\n{\"voltage\":3.35,\"current\":0.49,\"temperature\":36.2,\"rpm\":1350}"
  },
  {
    id: "modbus-poll",
    name: "Modbus 轮询",
    channel: "modbus",
    intervalMs: 1200,
    loop: true,
    lines: "01 03 00 00 00 04 44 09\n01 04 00 10 00 02 70 0E"
  }
];

export const defaultCanSignals: CanSignal[] = [
  { id: "battery", name: "Battery", startBit: 0, length: 8, scale: 0.1, offset: 0, unit: "V" },
  { id: "current", name: "Current", startBit: 8, length: 8, scale: 0.02, offset: 0, unit: "A" },
  { id: "temperature", name: "Temperature", startBit: 16, length: 8, scale: 1, offset: -40, unit: "C" },
  { id: "rpm", name: "RPM", startBit: 24, length: 16, scale: 1, offset: 0, unit: "rpm" }
];
