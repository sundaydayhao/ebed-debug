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

#ifdef EBED_HAS_MOSQUITTO
#include <QMetaObject>

#include <mosquitto.h>

void mqttConnectCallback(struct mosquitto *client, void *userdata, int rc);
void mqttDisconnectCallback(struct mosquitto *client, void *userdata, int rc);
void mqttMessageCallback(struct mosquitto *client, void *userdata, const struct mosquitto_message *message);
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
  return QStringLiteral("MQTT 后端未编译进当前程序，MQTT 通道不可用。请安装 libmosquitto-dev 后重新执行 cmake -S . -B build && cmake --build build。");
}

#ifdef EBED_HAS_MOSQUITTO
struct MosquittoRuntime {
  MosquittoRuntime() { mosquitto_lib_init(); }
  ~MosquittoRuntime() { mosquitto_lib_cleanup(); }
};

MosquittoRuntime &mosquittoRuntime() {
  static MosquittoRuntime runtime;
  return runtime;
}

QString mqttLibraryErrorText(int code) {
  const char *text = mosquitto_strerror(code);
  return QString::fromUtf8(text ? text : "unknown error");
}

QString mqttConnAckText(int code) {
  const char *text = mosquitto_connack_string(code);
  return QString::fromUtf8(text ? text : "unknown connack");
}
#endif

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

MqttTransportClient::~MqttTransportClient() {
  closeClient();
}

bool MqttTransportClient::isOpen() const {
#ifdef EBED_HAS_MOSQUITTO
  return m_connected;
#else
  return false;
#endif
}

void MqttTransportClient::open(const QVariantMap &config) {
#ifdef EBED_HAS_MOSQUITTO
  closeClient();
  mosquittoRuntime();
  const QUrl url(config.value("url").toString());
  if (!url.isValid() || url.host().isEmpty()) {
    emit statusChanged(QStringLiteral("MQTT Broker URL 无效。"), "error");
    return;
  }

  const QString scheme = url.scheme().isEmpty() ? QStringLiteral("mqtt") : url.scheme().toLower();
  if (scheme != "mqtt" && scheme != "mqtts") {
    emit statusChanged(QStringLiteral("MQTT 仅支持 mqtt:// 或 mqtts:// URL。"), "error");
    return;
  }

  const QByteArray clientId = config.value("clientId").toString().trimmed().toUtf8();
  m_client = mosquitto_new(clientId.isEmpty() ? nullptr : clientId.constData(), true, this);
  if (!m_client) {
    emit statusChanged(QStringLiteral("MQTT 客户端初始化失败。"), "error");
    return;
  }

  m_publishTopic = config.value("publishTopic").toString();
  m_subscribeTopic = config.value("subscribeTopic").toString();
  m_connected = false;
  m_closing = false;

  mosquitto_connect_callback_set(m_client, mqttConnectCallback);
  mosquitto_disconnect_callback_set(m_client, mqttDisconnectCallback);
  mosquitto_message_callback_set(m_client, mqttMessageCallback);

  const QByteArray username = config.value("username").toString().toUtf8();
  const QByteArray password = config.value("password").toByteArray();
  if (!username.isEmpty() || !password.isEmpty()) {
    const int authResult = mosquitto_username_pw_set(m_client, username.isEmpty() ? nullptr : username.constData(), password.constData());
    if (authResult != MOSQ_ERR_SUCCESS) {
      emit statusChanged(QString("MQTT 鉴权配置失败：%1").arg(mqttLibraryErrorText(authResult)), "error");
      mosquitto_destroy(m_client);
      m_client = nullptr;
      return;
    }
  }

  if (scheme == "mqtts") {
    const int tlsResult = mosquitto_tls_set(m_client, nullptr, "/etc/ssl/certs", nullptr, nullptr, nullptr);
    if (tlsResult != MOSQ_ERR_SUCCESS) {
      emit statusChanged(QString("MQTT TLS 配置失败：%1").arg(mqttLibraryErrorText(tlsResult)), "error");
      mosquitto_destroy(m_client);
      m_client = nullptr;
      return;
    }
  }

  const QByteArray host = url.host().toUtf8();
  const int port = url.port(scheme == "mqtts" ? 8883 : 1883);
  const int connectResult = mosquitto_connect_async(m_client, host.constData(), port, 60);
  if (connectResult != MOSQ_ERR_SUCCESS) {
    emit statusChanged(QString("MQTT 连接启动失败：%1").arg(mqttLibraryErrorText(connectResult)), "error");
    mosquitto_destroy(m_client);
    m_client = nullptr;
    return;
  }

  const int loopResult = mosquitto_loop_start(m_client);
  if (loopResult != MOSQ_ERR_SUCCESS) {
    emit statusChanged(QString("MQTT 后台循环启动失败：%1").arg(mqttLibraryErrorText(loopResult)), "error");
    mosquitto_destroy(m_client);
    m_client = nullptr;
  }
#else
  Q_UNUSED(config)
  emit statusChanged(mqttModuleUnavailableMessage(), "error");
#endif
}

void MqttTransportClient::closeClient() {
#ifdef EBED_HAS_MOSQUITTO
  if (!m_client) {
    return;
  }

  const bool wasConnected = m_connected;
  m_closing = true;

  mosquitto_connect_callback_set(m_client, nullptr);
  mosquitto_disconnect_callback_set(m_client, nullptr);
  mosquitto_message_callback_set(m_client, nullptr);
  mosquitto_disconnect(m_client);
  mosquitto_loop_stop(m_client, true);
  mosquitto_destroy(m_client);

  m_client = nullptr;
  m_connected = false;
  m_publishTopic.clear();
  m_subscribeTopic.clear();
  m_closing = false;

  if (wasConnected) {
    emit statusChanged("MQTT closed", "warn");
  }
#endif
}

void MqttTransportClient::sendPayload(const QString &payload, PayloadFormat format, const QVariantMap &meta) {
#ifdef EBED_HAS_MOSQUITTO
  if (m_client && m_connected) {
    const QString topic = meta.value("topic", m_publishTopic).toString();
    if (topic.trimmed().isEmpty()) {
      emit statusChanged(QStringLiteral("MQTT 发布 Topic 为空。"), "error");
      return;
    }

    const QByteArray bytes = payloadToBytes(payload, format);
    const QByteArray topicUtf8 = topic.toUtf8();
    const int publishResult = mosquitto_publish(
      m_client,
      nullptr,
      topicUtf8.constData(),
      bytes.size(),
      bytes.constData(),
      0,
      false
    );
    if (publishResult != MOSQ_ERR_SUCCESS) {
      emit statusChanged(QString("MQTT 发布失败：%1").arg(mqttLibraryErrorText(publishResult)), "error");
    }
  }
#else
  Q_UNUSED(payload)
  Q_UNUSED(format)
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
#ifdef EBED_HAS_MOSQUITTO
  return true;
#else
  return false;
#endif
}

#ifdef EBED_HAS_MOSQUITTO
void mqttConnectCallback(struct mosquitto *, void *userdata, int rc) {
  auto *client = static_cast<MqttTransportClient *>(userdata);
  if (!client) {
    return;
  }

  QMetaObject::invokeMethod(client, [client, rc]() {
    if (client->m_closing) {
      return;
    }

    if (rc != 0) {
      client->m_connected = false;
      emit client->statusChanged(QString("MQTT 连接失败：%1").arg(mqttConnAckText(rc)), "error");
      return;
    }

    client->m_connected = true;
    emit client->statusChanged("MQTT connected", "ok");

    if (!client->m_subscribeTopic.isEmpty() && client->m_client) {
      const QByteArray topic = client->m_subscribeTopic.toUtf8();
      const int subscribeResult = mosquitto_subscribe(client->m_client, nullptr, topic.constData(), 0);
      if (subscribeResult != MOSQ_ERR_SUCCESS) {
        emit client->statusChanged(QString("MQTT 订阅失败：%1").arg(mqttLibraryErrorText(subscribeResult)), "error");
      }
    }
  }, Qt::QueuedConnection);
}

void mqttDisconnectCallback(struct mosquitto *, void *userdata, int rc) {
  auto *client = static_cast<MqttTransportClient *>(userdata);
  if (!client) {
    return;
  }

  QMetaObject::invokeMethod(client, [client, rc]() {
    if (client->m_closing) {
      return;
    }

    client->m_connected = false;
    if (rc == 0) {
      emit client->statusChanged("MQTT closed", "warn");
    } else {
      emit client->statusChanged(QString("MQTT 连接中断：%1").arg(mqttLibraryErrorText(rc)), "error");
    }
  }, Qt::QueuedConnection);
}

void mqttMessageCallback(struct mosquitto *, void *userdata, const struct mosquitto_message *message) {
  auto *client = static_cast<MqttTransportClient *>(userdata);
  if (!client || !message || !message->topic) {
    return;
  }

  const QByteArray payload(static_cast<const char *>(message->payload), message->payloadlen);
  const QString topic = QString::fromUtf8(message->topic);

  QMetaObject::invokeMethod(client, [client, payload, topic]() {
    emit client->dataReceived(QString::fromUtf8(payload), PayloadFormat::Text, topic);
  }, Qt::QueuedConnection);
}
#endif
