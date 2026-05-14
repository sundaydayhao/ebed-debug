#include "TransportClient.h"

#include "ProtocolUtils.h"

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUrl>

#ifdef EBED_HAS_QT_SERIALPORT
#include <QSerialPort>
#include <QSerialPortInfo>
#endif

#ifdef EBED_HAS_QT_MQTT
#include <QMqttClient>
#include <QMqttTopicFilter>
#include <QMqttTopicName>
#endif

namespace {

QByteArray payloadToBytes(const QString &payload, PayloadFormat format) {
  if (format == PayloadFormat::Hex) {
    return hexToBytes(payload);
  }
  if (format == PayloadFormat::Json) {
    return payload.toUtf8();
  }
  return payload.toUtf8();
}

QString formatSerialError(const QString &message, const QString &devicePath) {
  const QString lower = message.toLower();
  if (lower.contains("permission") || lower.contains("access") || lower.contains("eacces") || lower.contains("eperm")) {
    return QStringLiteral("串口权限不足，无法打开 %1。Linux 下请执行 sudo usermod -aG dialout $USER 后注销并重新登录。原始错误：%2")
      .arg(devicePath, message);
  }
  return message;
}

QString serialModuleUnavailableMessage() {
  return QStringLiteral("Qt SerialPort 模块未编译进当前程序，串口 / Modbus RTU 不可用。请安装 qt6-serialport-dev 后重新执行 cmake -S . -B build && cmake --build build。");
}

QString mqttModuleUnavailableMessage() {
  return QStringLiteral("Qt MQTT 模块未编译进当前程序，MQTT 通道不可用。请安装 qt6-mqtt-dev 后重新执行 cmake -S . -B build && cmake --build build。");
}

}  // namespace

AbstractTransportClient::AbstractTransportClient(ChannelKey channel, QObject *parent) : QObject(parent), m_channel(channel) {}

ChannelKey AbstractTransportClient::channel() const {
  return m_channel;
}

TcpTransportClient::TcpTransportClient(ChannelKey channel, QObject *parent)
  : AbstractTransportClient(channel, parent), m_socket(new QTcpSocket(this)) {
  connect(m_socket, &QTcpSocket::connected, this, [this]() { emit statusChanged("TCP connected", "ok"); });
  connect(m_socket, &QTcpSocket::readyRead, this, [this]() {
    emit dataReceived(bytesToHex(m_socket->readAll()), PayloadFormat::Hex, {});
  });
  connect(m_socket, &QTcpSocket::disconnected, this, [this]() { emit statusChanged("TCP closed", "warn"); });
  connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    emit statusChanged(m_socket->errorString(), "error");
  });
}

bool TcpTransportClient::isOpen() const {
  return m_socket->state() == QAbstractSocket::ConnectedState;
}

void TcpTransportClient::open(const QVariantMap &config) {
  if (isOpen()) {
    m_socket->disconnectFromHost();
  }
  m_socket->connectToHost(config.value("host").toString(), static_cast<quint16>(config.value("port").toInt()));
}

void TcpTransportClient::closeClient() {
  if (m_socket->state() != QAbstractSocket::UnconnectedState) {
    m_socket->disconnectFromHost();
  }
}

void TcpTransportClient::sendPayload(const QString &payload, PayloadFormat format, const QVariantMap &) {
  const QByteArray bytes = payloadToBytes(payload, format);
  if (!bytes.isEmpty()) {
    m_socket->write(bytes);
  }
}

UdpTransportClient::UdpTransportClient(ChannelKey channel, QObject *parent)
  : AbstractTransportClient(channel, parent), m_socket(new QUdpSocket(this)) {
  connect(m_socket, &QUdpSocket::readyRead, this, [this]() {
    while (m_socket->hasPendingDatagrams()) {
      QNetworkDatagram datagram = m_socket->receiveDatagram();
      const QString meta = QString("%1:%2").arg(datagram.senderAddress().toString()).arg(datagram.senderPort());
      emit dataReceived(bytesToHex(datagram.data()), PayloadFormat::Hex, meta);
    }
  });
  connect(m_socket, &QUdpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    emit statusChanged(m_socket->errorString(), "error");
  });
}

bool UdpTransportClient::isOpen() const {
  return m_socket->state() == QAbstractSocket::BoundState;
}

void UdpTransportClient::open(const QVariantMap &config) {
  m_config = config;
  m_socket->close();
  const quint16 port = static_cast<quint16>(config.value("localPort").toInt());
  if (m_socket->bind(QHostAddress::AnyIPv4, port)) {
    emit statusChanged(QString("UDP listening 0.0.0.0:%1").arg(m_socket->localPort()), "ok");
  } else {
    emit statusChanged(m_socket->errorString(), "error");
  }
}

void UdpTransportClient::closeClient() {
  m_socket->close();
  emit statusChanged("UDP closed", "warn");
}

void UdpTransportClient::sendPayload(const QString &payload, PayloadFormat format, const QVariantMap &meta) {
  const QString host = meta.value("host", m_config.value("remoteHost", m_config.value("host"))).toString();
  const quint16 port = static_cast<quint16>(meta.value("port", m_config.value("remotePort", m_config.value("port"))).toInt());
  const QByteArray bytes = payloadToBytes(payload, format);
  m_socket->writeDatagram(bytes, QHostAddress(host), port);
}

SerialTransportClient::SerialTransportClient(ChannelKey channel, QObject *parent) : AbstractTransportClient(channel, parent) {}

bool SerialTransportClient::isOpen() const {
#ifdef EBED_HAS_QT_SERIALPORT
  const auto *serial = findChild<QSerialPort *>();
  return serial && serial->isOpen();
#else
  return false;
#endif
}

void SerialTransportClient::open(const QVariantMap &config) {
#ifdef EBED_HAS_QT_SERIALPORT
  QSerialPort *serial = findChild<QSerialPort *>();
  if (!serial) {
    serial = new QSerialPort(this);
    connect(serial, &QSerialPort::readyRead, this, [this, serial]() {
      emit dataReceived(bytesToHex(serial->readAll()), PayloadFormat::Hex, {});
    });
    connect(serial, &QSerialPort::errorOccurred, this, [this, serial](QSerialPort::SerialPortError error) {
      if (error != QSerialPort::NoError) {
        emit statusChanged(serial->errorString(), "error");
      }
    });
  }

  if (serial->isOpen()) {
    serial->close();
  }

  serial->setPortName(config.value("path").toString());
  serial->setBaudRate(config.value("baudRate").toInt());
  serial->setDataBits(static_cast<QSerialPort::DataBits>(config.value("dataBits", 8).toInt()));
  serial->setStopBits(config.value("stopBits", 1).toInt() == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop);
  const QString parity = config.value("parity", "none").toString();
  serial->setParity(parity == "even" ? QSerialPort::EvenParity : parity == "odd" ? QSerialPort::OddParity : QSerialPort::NoParity);

  if (serial->open(QIODevice::ReadWrite)) {
    emit statusChanged("Serial opened", "ok");
  } else {
    emit statusChanged(formatSerialError(serial->errorString(), config.value("path").toString()), "error");
  }
#else
  Q_UNUSED(config)
  emit statusChanged(serialModuleUnavailableMessage(), "error");
#endif
}

void SerialTransportClient::closeClient() {
#ifdef EBED_HAS_QT_SERIALPORT
  if (auto *serial = findChild<QSerialPort *>(); serial && serial->isOpen()) {
    serial->close();
    emit statusChanged("Serial closed", "warn");
  }
#endif
}

void SerialTransportClient::sendPayload(const QString &payload, PayloadFormat format, const QVariantMap &) {
#ifdef EBED_HAS_QT_SERIALPORT
  if (auto *serial = findChild<QSerialPort *>(); serial && serial->isOpen()) {
    serial->write(payloadToBytes(payload, format));
  }
#else
  Q_UNUSED(payload)
  Q_UNUSED(format)
#endif
}

MqttTransportClient::MqttTransportClient(ChannelKey channel, QObject *parent) : AbstractTransportClient(channel, parent) {}

bool MqttTransportClient::isOpen() const {
#ifdef EBED_HAS_QT_MQTT
  const auto *client = findChild<QMqttClient *>();
  return client && client->state() == QMqttClient::Connected;
#else
  return false;
#endif
}

void MqttTransportClient::open(const QVariantMap &config) {
#ifdef EBED_HAS_QT_MQTT
  QMqttClient *client = findChild<QMqttClient *>();
  if (!client) {
    client = new QMqttClient(this);
    connect(client, &QMqttClient::connected, this, [this, client, config]() {
      emit statusChanged("MQTT connected", "ok");
      const QString subscribeTopic = config.value("subscribeTopic").toString();
      if (!subscribeTopic.isEmpty()) {
        client->subscribe(QMqttTopicFilter(subscribeTopic));
      }
    });
    connect(client, &QMqttClient::disconnected, this, [this]() { emit statusChanged("MQTT closed", "warn"); });
    connect(client, &QMqttClient::messageReceived, this, [this](const QByteArray &message, const QMqttTopicName &topic) {
      emit dataReceived(QString::fromUtf8(message), PayloadFormat::Text, topic.name());
    });
    connect(client, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError error) {
      if (error != QMqttClient::NoError) {
        emit statusChanged(QString("MQTT error %1").arg(static_cast<int>(error)), "error");
      }
    });
  }

  const QUrl url(config.value("url").toString());
  client->setHostname(url.host());
  client->setPort(url.port(url.scheme() == "mqtts" ? 8883 : 1883));
  client->setUsername(config.value("username").toString());
  client->setPassword(config.value("password").toByteArray());
  client->setClientId(config.value("clientId").toString());
  client->connectToHost();
#else
  Q_UNUSED(config)
  emit statusChanged(mqttModuleUnavailableMessage(), "error");
#endif
}

void MqttTransportClient::closeClient() {
#ifdef EBED_HAS_QT_MQTT
  if (auto *client = findChild<QMqttClient *>(); client) {
    client->disconnectFromHost();
  }
#endif
}

void MqttTransportClient::sendPayload(const QString &payload, PayloadFormat, const QVariantMap &meta) {
#ifdef EBED_HAS_QT_MQTT
  if (auto *client = findChild<QMqttClient *>(); client && client->state() == QMqttClient::Connected) {
    const QString topic = meta.value("topic").toString();
    client->publish(QMqttTopicName(topic), payload.toUtf8());
  }
#else
  Q_UNUSED(payload)
  Q_UNUSED(meta)
#endif
}

QStringList availableSerialPorts() {
#ifdef EBED_HAS_QT_SERIALPORT
  QStringList ports;
  const auto entries = QSerialPortInfo::availablePorts();
  for (const QSerialPortInfo &entry : entries) {
    ports.append(entry.portName());
  }
  return ports;
#else
  return {};
#endif
}

bool serialSupportAvailable() {
#ifdef EBED_HAS_QT_SERIALPORT
  return true;
#else
  return false;
#endif
}

bool mqttSupportAvailable() {
#ifdef EBED_HAS_QT_MQTT
  return true;
#else
  return false;
#endif
}
