#pragma once

#include "Domain.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

QString nowStamp(const QDateTime &dateTime = QDateTime::currentDateTime());
int byteLength(const QString &payload, PayloadFormat format);
QString normalizeHex(const QString &value);
QByteArray hexToBytes(const QString &value);
QString bytesToHex(const QByteArray &bytes);
QString textToHex(const QString &value);
QString hexToAscii(const QString &value);
quint16 crc16Modbus(const QByteArray &bytes);
QString buildModbusFrame(const ModbusBuilderState &state);
QString decodeModbusFrame(const QString &frame);
CanFrame parseCanFrameText(const QString &input, bool *ok = nullptr);
QVector<DecodedCanSignal> decodeCanSignals(const CanFrame &frame, const QVector<CanSignal> &canSignals);
QString decodePayload(ChannelKey channel, const QString &payload, PayloadFormat format);
ChartPoint extractMetrics(const LogEntry &entry, const ChartPoint &previous);
QString logsToCsv(const QVector<LogEntry> &logs);
QString buildHtmlReport(const QVector<LogEntry> &logs, const QVector<ChartPoint> &chartPoints);
PayloadFormat inferFormat(const QString &payload);
