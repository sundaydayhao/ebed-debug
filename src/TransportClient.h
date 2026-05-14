#pragma once

#include "Domain.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>

struct mosquitto;

class AbstractTransportClient : public QObject {
  Q_OBJECT

public:
  explicit AbstractTransportClient(ChannelKey channel, QObject *parent = nullptr);
  ~AbstractTransportClient() override = default;

  ChannelKey channel() const;
  virtual bool isOpen() const = 0;

public slots:
  virtual void open(const QVariantMap &config) = 0;
  virtual void closeClient() = 0;
  virtual void sendPayload(const QString &payload, PayloadFormat format, const QVariantMap &meta = {}) = 0;

signals:
  void dataReceived(const QString &payload, PayloadFormat format, const QString &meta);
  void statusChanged(const QString &message, const QString &level);

protected:
  ChannelKey m_channel;
};

class TcpTransportClient : public AbstractTransportClient {
  Q_OBJECT

public:
  explicit TcpTransportClient(ChannelKey channel, QObject *parent = nullptr);
  bool isOpen() const override;

public slots:
  void open(const QVariantMap &config) override;
  void closeClient() override;
  void sendPayload(const QString &payload, PayloadFormat format, const QVariantMap &meta = {}) override;

private:
  class QTcpSocket *m_socket;
};

class UdpTransportClient : public AbstractTransportClient {
  Q_OBJECT

public:
  explicit UdpTransportClient(ChannelKey channel, QObject *parent = nullptr);
  bool isOpen() const override;

public slots:
  void open(const QVariantMap &config) override;
  void closeClient() override;
  void sendPayload(const QString &payload, PayloadFormat format, const QVariantMap &meta = {}) override;

private:
  class QUdpSocket *m_socket;
  QVariantMap m_config;
};

class SerialTransportClient : public AbstractTransportClient {
  Q_OBJECT

public:
  explicit SerialTransportClient(ChannelKey channel, QObject *parent = nullptr);
  bool isOpen() const override;

public slots:
  void open(const QVariantMap &config) override;
  void closeClient() override;
  void sendPayload(const QString &payload, PayloadFormat format, const QVariantMap &meta = {}) override;
};

class MqttTransportClient : public AbstractTransportClient {
  Q_OBJECT

public:
  explicit MqttTransportClient(ChannelKey channel, QObject *parent = nullptr);
  ~MqttTransportClient() override;
  bool isOpen() const override;

public slots:
  void open(const QVariantMap &config) override;
  void closeClient() override;
  void sendPayload(const QString &payload, PayloadFormat format, const QVariantMap &meta = {}) override;

private:
  friend void mqttConnectCallback(struct mosquitto *client, void *userdata, int rc);
  friend void mqttDisconnectCallback(struct mosquitto *client, void *userdata, int rc);
  friend void mqttMessageCallback(struct mosquitto *client, void *userdata, const struct mosquitto_message *message);

  struct mosquitto *m_client = nullptr;
  QString m_publishTopic;
  QString m_subscribeTopic;
  bool m_connected = false;
  bool m_closing = false;
};

QStringList availableSerialPorts();
bool serialSupportAvailable();
bool mqttSupportAvailable();
