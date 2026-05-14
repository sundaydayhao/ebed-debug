#include "TemplateRepository.h"

QVector<ChannelInfo> defaultChannels() {
  return {
    {ChannelKey::Serial, QStringLiteral("串口调试"), "串口", QStringLiteral("ESP32 / STM32 / GD32 UART"), QColor("#2563eb")},
    {ChannelKey::Tcp, QStringLiteral("TCP 调试"), "TCP", QStringLiteral("Socket 客户端收发"), QColor("#059669")},
    {ChannelKey::Udp, QStringLiteral("UDP 调试"), "UDP", QStringLiteral("网关广播与遥测"), QColor("#d97706")},
    {ChannelKey::Mqtt, QStringLiteral("MQTT 调试"), "MQTT", QStringLiteral("Broker 订阅与发布"), QColor("#7c3aed")},
    {ChannelKey::Modbus, QStringLiteral("Modbus"), "Modbus", QStringLiteral("RTU / TCP 帧构造解析"), QColor("#0f766e")},
    {ChannelKey::Can, QStringLiteral("CAN 解析"), "CAN", QStringLiteral("帧解析与信号换算"), QColor("#be123c")}
  };
}

QVector<ProtocolTemplate> defaultProtocolTemplates() {
  return {
    {
      "esp32-at",
      QStringLiteral("ESP32 AT/WiFi 快测"),
      "ESP32",
      QStringLiteral("串口 AT 指令、WiFi 状态查询和 MQTT 示例载荷。"),
      {
        {"esp32-at", QStringLiteral("AT 握手"), ChannelKey::Serial, "AT\r\n", PayloadFormat::Text, QStringLiteral("验证 ESP-AT 固件串口响应。")},
        {"esp32-wifi", QStringLiteral("查询 WiFi"), ChannelKey::Serial, "AT+CWJAP?\r\n", PayloadFormat::Text, QStringLiteral("读取当前接入点信息。")},
        {"esp32-json", QStringLiteral("Telemetry JSON"), ChannelKey::Mqtt, "{\"voltage\":3.31,\"current\":0.46,\"temperature\":36.5,\"rpm\":1320}", PayloadFormat::Json, QStringLiteral("模拟设备上报数据，用于图表解析。")}
      }
    },
    {
      "stm32-boot",
      QStringLiteral("STM32 Bootloader"),
      "STM32",
      QStringLiteral("常见 Bootloader 同步、读 ID 和擦除命令模板。"),
      {
        {"stm32-sync", QStringLiteral("同步 0x7F"), ChannelKey::Serial, "7F", PayloadFormat::Hex, QStringLiteral("进入系统 Bootloader 后发送同步字节。")},
        {"stm32-get-id", QStringLiteral("读取芯片 ID"), ChannelKey::Serial, "02 FD", PayloadFormat::Hex, QStringLiteral("STM32 Bootloader Get ID 命令。")},
        {"stm32-ping", QStringLiteral("板级 Ping"), ChannelKey::Tcp, "{\"cmd\":\"ping\",\"seq\":1}", PayloadFormat::Json, QStringLiteral("用于运行在以太网/串口网关上的自定义固件。")}
      }
    },
    {
      "gd32-field",
      QStringLiteral("GD32 现场采集"),
      "GD32",
      QStringLiteral("串口日志、Modbus 寄存器和 UDP 遥测组合。"),
      {
        {"gd32-status", QStringLiteral("STATUS?"), ChannelKey::Serial, "STATUS?\r\n", PayloadFormat::Text, QStringLiteral("查询固件状态机。")},
        {"gd32-holding", QStringLiteral("读保持寄存器"), ChannelKey::Modbus, "01 03 00 00 00 08 44 0C", PayloadFormat::Hex, QStringLiteral("读取 0x0000 起始的 8 个 holding registers。")},
        {"gd32-udp", QStringLiteral("UDP 校准"), ChannelKey::Udp, "{\"cmd\":\"calibrate\",\"axis\":\"x\"}", PayloadFormat::Json, QStringLiteral("向现场网关发送校准指令。")}
      }
    },
    {
      "industrial-can",
      QStringLiteral("工控 CAN/Modbus"),
      "工控",
      QStringLiteral("CAN 帧解析、Modbus TCP 和控制指令。"),
      {
        {"can-motor", QStringLiteral("电机状态帧"), ChannelKey::Can, "18FF50E5#211F245804000001", PayloadFormat::Text, QStringLiteral("J1939 风格的 29-bit 扩展帧样例。")},
        {"modbus-tcp", QStringLiteral("Modbus TCP 读输入"), ChannelKey::Modbus, "00 01 00 00 00 06 01 04 00 00 00 04", PayloadFormat::Hex, QStringLiteral("MBAP + FC04 读取输入寄存器。")},
        {"tcp-stop", QStringLiteral("急停命令"), ChannelKey::Tcp, "{\"cmd\":\"stop\",\"reason\":\"operator\"}", PayloadFormat::Json, QStringLiteral("自定义 TCP 控制协议示例。")}
      }
    }
  };
}

QVector<AutoScript> defaultStarterScripts() {
  return {
    {"heartbeat", QStringLiteral("设备心跳"), ChannelKey::Serial, 1000, true, "PING\r\n\nSTATUS?\r\n"},
    {"mqtt-telemetry", QStringLiteral("MQTT 遥测压测"), ChannelKey::Mqtt, 750, true, "{\"voltage\":3.32,\"current\":0.41,\"temperature\":35.7,\"rpm\":1280}\n{\"voltage\":3.35,\"current\":0.49,\"temperature\":36.2,\"rpm\":1350}"},
    {"modbus-poll", QStringLiteral("Modbus 轮询"), ChannelKey::Modbus, 1200, true, "01 03 00 00 00 04 44 09\n01 04 00 10 00 02 70 0E"}
  };
}

QVector<CanSignal> defaultCanSignals() {
  return {
    {"battery", "Battery", 0, 8, 0.1, 0.0, "V"},
    {"current", "Current", 8, 8, 0.02, 0.0, "A"},
    {"temperature", "Temperature", 16, 8, 1.0, -40.0, "C"},
    {"rpm", "RPM", 24, 16, 1.0, 0.0, "rpm"}
  };
}
