#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>
#include <QVector>

enum class ChannelKey {
  Serial,
  Tcp,
  Udp,
  Mqtt,
  Modbus,
  Can
};

enum class PayloadFormat {
  Text,
  Hex,
  Json,
  Event
};

enum class Direction {
  Rx,
  Tx,
  Sys
};

struct ChannelInfo {
  ChannelKey key;
  QString name;
  QString shortName;
  QString summary;
  QColor accent;
};

struct LogEntry {
  QString id;
  QString ts;
  ChannelKey channel;
  Direction direction;
  PayloadFormat format;
  QString payload;
  QString decoded;
  int bytes = 0;
  QString meta;
};

struct ChartPoint {
  int id = 0;
  QString time;
  double voltage = 0.0;
  double current = 0.0;
  double temperature = 0.0;
  double rpm = 0.0;
  ChannelKey channel = ChannelKey::Serial;
};

struct ProtocolCommand {
  QString id;
  QString name;
  ChannelKey channel;
  QString payload;
  PayloadFormat format;
  QString description;
};

struct ProtocolTemplate {
  QString id;
  QString name;
  QString target;
  QString description;
  QVector<ProtocolCommand> commands;
};

struct AutoScript {
  QString id;
  QString name;
  ChannelKey channel = ChannelKey::Serial;
  int intervalMs = 1000;
  bool loop = true;
  QString lines;
};

struct CanSignal {
  QString id;
  QString name;
  int startBit = 0;
  int length = 8;
  double scale = 1.0;
  double offset = 0.0;
  QString unit;
};

struct DecodedCanSignal {
  QString id;
  QString name;
  int startBit = 0;
  int length = 0;
  double scale = 1.0;
  double offset = 0.0;
  QString unit;
  quint32 raw = 0;
  double value = 0.0;
};

struct CanFrame {
  QString id;
  int dlc = 0;
  QString data;
  bool extended = false;
};

struct ModbusBuilderState {
  QString variant = "rtu";
  int unitId = 1;
  int functionCode = 3;
  int address = 0;
  int quantity = 4;
  int value = 1;
  int transactionId = 1;
};

struct SerialConfig {
  QString path = "/dev/ttyUSB0";
  int baudRate = 115200;
  int dataBits = 8;
  int stopBits = 1;
  QString parity = "none";
};

struct TcpConfig {
  QString host = "127.0.0.1";
  int port = 9000;
};

struct UdpConfig {
  int localPort = 7001;
  QString remoteHost = "127.0.0.1";
  int remotePort = 7002;
};

struct MqttConfig {
  QString url = "mqtt://broker.emqx.io:1883";
  QString clientId = "ebed-debug-pro";
  QString subscribeTopic = "ebed/debug/rx";
  QString publishTopic = "ebed/debug/tx";
  QString username;
  QString password;
};

struct ModbusEndpoint {
  QString tcpHost = "127.0.0.1";
  int tcpPort = 502;
  QString serialPath = "/dev/ttyUSB0";
  int baudRate = 9600;
};

inline QString channelKeyToString(ChannelKey channel) {
  switch (channel) {
    case ChannelKey::Serial:
      return "serial";
    case ChannelKey::Tcp:
      return "tcp";
    case ChannelKey::Udp:
      return "udp";
    case ChannelKey::Mqtt:
      return "mqtt";
    case ChannelKey::Modbus:
      return "modbus";
    case ChannelKey::Can:
      return "can";
  }
  return "unknown";
}

inline QString channelKeyToDisplayName(ChannelKey channel) {
  switch (channel) {
    case ChannelKey::Serial:
      return QStringLiteral("串口调试");
    case ChannelKey::Tcp:
      return QStringLiteral("TCP 调试");
    case ChannelKey::Udp:
      return QStringLiteral("UDP 调试");
    case ChannelKey::Mqtt:
      return QStringLiteral("MQTT 调试");
    case ChannelKey::Modbus:
      return QStringLiteral("Modbus");
    case ChannelKey::Can:
      return QStringLiteral("CAN 解析");
  }
  return QStringLiteral("未知通道");
}

inline QString payloadFormatToString(PayloadFormat format) {
  switch (format) {
    case PayloadFormat::Text:
      return "text";
    case PayloadFormat::Hex:
      return "hex";
    case PayloadFormat::Json:
      return "json";
    case PayloadFormat::Event:
      return "event";
  }
  return "text";
}

inline QString directionToString(Direction direction) {
  switch (direction) {
    case Direction::Rx:
      return "rx";
    case Direction::Tx:
      return "tx";
    case Direction::Sys:
      return "sys";
  }
  return "sys";
}

Q_DECLARE_METATYPE(ChannelKey)
Q_DECLARE_METATYPE(PayloadFormat)
Q_DECLARE_METATYPE(Direction)
