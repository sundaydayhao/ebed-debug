#include "ProtocolUtils.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QtMath>

namespace {

QString escapeHtml(const QString &value) {
  QString escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#039;");
  return escaped;
}

void appendWord(QByteArray &bytes, int value) {
  bytes.append(static_cast<char>((value >> 8) & 0xff));
  bytes.append(static_cast<char>(value & 0xff));
}

double clampMetric(double value, double min, double max) {
  if (!qIsFinite(value)) {
    return min;
  }
  return qBound(min, value, max);
}

QString truncateText(const QString &value, int length = 96) {
  if (value.size() <= length) {
    return value;
  }
  return value.left(length) + "...";
}

QJsonObject tryParseObject(const QString &text) {
  const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
  if (!document.isObject()) {
    return {};
  }
  return document.object();
}

quint32 extractUnsignedLittleEndian(const QByteArray &bytes, int startBit, int length) {
  quint32 raw = 0;
  for (int bit = 0; bit < length; ++bit) {
    const int absolute = startBit + bit;
    const int byteIndex = absolute / 8;
    const int bitIndex = absolute % 8;
    if (byteIndex >= bytes.size()) {
      break;
    }
    const quint8 value = static_cast<quint8>(bytes.at(byteIndex));
    if ((value >> bitIndex) & 0x1) {
      raw |= (1u << bit);
    }
  }
  return raw;
}

}  // namespace

QString nowStamp(const QDateTime &dateTime) {
  return dateTime.toString("HH:mm:ss");
}

int byteLength(const QString &payload, PayloadFormat format) {
  if (format == PayloadFormat::Hex) {
    return hexToBytes(payload).size();
  }
  return payload.toUtf8().size();
}

QString normalizeHex(const QString &value) {
  QString clean = value;
  clean.remove(QRegularExpression("0x", QRegularExpression::CaseInsensitiveOption));
  clean.remove(QRegularExpression("[^A-Fa-f0-9]"));

  QStringList groups;
  groups.reserve(clean.size() / 2 + 1);
  for (int index = 0; index < clean.size(); index += 2) {
    groups.append(clean.mid(index, 2).toUpper());
  }
  return groups.join(' ').trimmed();
}

QByteArray hexToBytes(const QString &value) {
  QString clean = value;
  clean.remove(QRegularExpression("0x", QRegularExpression::CaseInsensitiveOption));
  clean.remove(QRegularExpression("[^A-Fa-f0-9]"));
  if (clean.size() % 2 != 0) {
    clean.prepend('0');
  }
  return QByteArray::fromHex(clean.toLatin1());
}

QString bytesToHex(const QByteArray &bytes) {
  return QString::fromLatin1(bytes.toHex(' ')).toUpper();
}

QString textToHex(const QString &value) {
  return bytesToHex(value.toUtf8());
}

QString hexToAscii(const QString &value) {
  const QByteArray bytes = hexToBytes(value);
  QString text;
  text.reserve(bytes.size());
  for (char byte : bytes) {
    const uchar valueByte = static_cast<uchar>(byte);
    if (valueByte >= 32 && valueByte <= 126) {
      text.append(QChar::fromLatin1(byte));
    } else {
      text.append('.');
    }
  }
  return text;
}

quint16 crc16Modbus(const QByteArray &bytes) {
  quint16 crc = 0xffff;
  for (char byte : bytes) {
    crc ^= static_cast<quint8>(byte);
    for (int bit = 0; bit < 8; ++bit) {
      const bool lsb = crc & 0x1;
      crc >>= 1;
      if (lsb) {
        crc ^= 0xa001;
      }
    }
  }
  return crc;
}

QString buildModbusFrame(const ModbusBuilderState &state) {
  QByteArray pdu;
  pdu.append(static_cast<char>(state.functionCode));
  appendWord(pdu, state.address);

  if (state.functionCode == 6) {
    appendWord(pdu, state.value);
  } else if (state.functionCode == 16) {
    appendWord(pdu, state.quantity);
    pdu.append(static_cast<char>(state.quantity * 2));
    for (int index = 0; index < state.quantity; ++index) {
      appendWord(pdu, state.value + index);
    }
  } else {
    appendWord(pdu, state.quantity);
  }

  if (state.variant == "tcp") {
    QByteArray frame;
    const int length = pdu.size() + 1;
    appendWord(frame, state.transactionId);
    appendWord(frame, 0);
    appendWord(frame, length);
    frame.append(static_cast<char>(state.unitId));
    frame.append(pdu);
    return bytesToHex(frame);
  }

  QByteArray frame;
  frame.append(static_cast<char>(state.unitId));
  frame.append(pdu);
  const quint16 crc = crc16Modbus(frame);
  frame.append(static_cast<char>(crc & 0xff));
  frame.append(static_cast<char>((crc >> 8) & 0xff));
  return bytesToHex(frame);
}

QString decodeModbusFrame(const QString &frame) {
  const QByteArray bytes = hexToBytes(frame);
  if (bytes.size() < 5) {
    return QStringLiteral("Modbus: 帧长度不足");
  }

  const bool isTcp = bytes.size() >= 8 && bytes.at(2) == 0x00 && bytes.at(3) == 0x00;
  const int unitIndex = isTcp ? 6 : 0;
  const int fnIndex = isTcp ? 7 : 1;
  const int dataIndex = isTcp ? 8 : 2;

  if (bytes.size() <= fnIndex) {
    return QStringLiteral("Modbus: 帧长度不足");
  }

  const int unit = static_cast<quint8>(bytes.at(unitIndex));
  const int fn = static_cast<quint8>(bytes.at(fnIndex));
  QString crcText = isTcp ? QStringLiteral("MBAP OK") : QStringLiteral("无 CRC");
  if (!isTcp && bytes.size() >= 6) {
    const QByteArray body = bytes.left(bytes.size() - 2);
    const quint16 expect = crc16Modbus(body);
    const quint16 actual =
      (static_cast<quint8>(bytes.at(bytes.size() - 1)) << 8) + static_cast<quint8>(bytes.at(bytes.size() - 2));
    crcText = expect == actual ? QStringLiteral("CRC OK") : QStringLiteral("CRC ERR");
  }

  if (fn & 0x80) {
    const int code = dataIndex < bytes.size() ? static_cast<quint8>(bytes.at(dataIndex)) : 0;
    return QString("%1: Unit %2, 异常功能码 0x%3, code %4, %5")
      .arg(isTcp ? "Modbus TCP" : "Modbus RTU")
      .arg(unit)
      .arg(QString::number(fn, 16))
      .arg(code)
      .arg(crcText);
  }

  if ((fn == 3 || fn == 4) && dataIndex < bytes.size()) {
    const int byteCount = static_cast<quint8>(bytes.at(dataIndex));
    QStringList registers;
    for (int index = dataIndex + 1; index + 1 < dataIndex + 1 + byteCount && index + 1 < bytes.size(); index += 2) {
      const int value = (static_cast<quint8>(bytes.at(index)) << 8) + static_cast<quint8>(bytes.at(index + 1));
      registers.append(QString::number(value));
    }
    return QString("%1: Unit %2, FC%3, %4 registers [%5], %6")
      .arg(isTcp ? "Modbus TCP" : "Modbus RTU")
      .arg(unit)
      .arg(fn)
      .arg(registers.size())
      .arg(registers.join(", "))
      .arg(crcText);
  }

  return QString("%1: Unit %2, FC%3, %4 bytes, %5")
    .arg(isTcp ? "Modbus TCP" : "Modbus RTU")
    .arg(unit)
    .arg(fn)
    .arg(bytes.size())
    .arg(crcText);
}

CanFrame parseCanFrameText(const QString &input, bool *ok) {
  const QString trimmed = input.trimmed();
  QRegularExpression candump("^([0-9A-Fa-f]{3,8})#([0-9A-Fa-f]*)$");
  const QRegularExpressionMatch match = candump.match(trimmed);
  if (match.hasMatch()) {
    if (ok) {
      *ok = true;
    }
    const QString data = normalizeHex(match.captured(2));
    return CanFrame{
      match.captured(1).toUpper(),
      qMin(8, hexToBytes(match.captured(2)).size()),
      data,
      match.captured(1).size() > 3
    };
  }

  const QStringList parts = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
  if (parts.size() >= 2) {
    QString id = parts.first();
    id.remove(QRegularExpression("^0x", QRegularExpression::CaseInsensitiveOption));
    if (ok) {
      *ok = true;
    }
    const QString data = normalizeHex(parts.mid(1).join(' '));
    return CanFrame{id.toUpper(), hexToBytes(data).size(), data, id.size() > 3};
  }

  if (ok) {
    *ok = false;
  }
  return {};
}

QVector<DecodedCanSignal> decodeCanSignals(const CanFrame &frame, const QVector<CanSignal> &canSignals) {
  const QByteArray bytes = hexToBytes(frame.data);
  QVector<DecodedCanSignal> decoded;
  decoded.reserve(canSignals.size());
  for (const CanSignal &signal : canSignals) {
    const quint32 raw = extractUnsignedLittleEndian(bytes, signal.startBit, signal.length);
    decoded.append(DecodedCanSignal{
      signal.id,
      signal.name,
      signal.startBit,
      signal.length,
      signal.scale,
      signal.offset,
      signal.unit,
      raw,
      raw * signal.scale + signal.offset
    });
  }
  return decoded;
}

QString decodePayload(ChannelKey channel, const QString &payload, PayloadFormat format) {
  if (format == PayloadFormat::Hex) {
    if (channel == ChannelKey::Modbus) {
      return decodeModbusFrame(payload);
    }
    return QString("HEX %1 | ASCII \"%2\"").arg(normalizeHex(payload), hexToAscii(payload));
  }

  if (format == PayloadFormat::Json) {
    const QJsonObject object = tryParseObject(payload);
    if (object.isEmpty()) {
      return QStringLiteral("JSON 解析失败");
    }
    QStringList items;
    for (auto it = object.begin(); it != object.end() && items.size() < 6; ++it) {
      if (it.value().isDouble()) {
        items.append(QString("%1: %2").arg(it.key(), QString::number(it.value().toDouble())));
      } else if (it.value().isString()) {
        items.append(QString("%1: %2").arg(it.key(), it.value().toString()));
      } else if (it.value().isBool()) {
        items.append(QString("%1: %2").arg(it.key(), it.value().toBool() ? "true" : "false"));
      } else {
        items.append(QString("%1: ...").arg(it.key()));
      }
    }
    return items.join(", ");
  }

  if (channel == ChannelKey::Can) {
    bool ok = false;
    const CanFrame frame = parseCanFrameText(payload, &ok);
    if (ok) {
      return QString("CAN %1, DLC %2, DATA %3").arg(frame.id).arg(frame.dlc).arg(frame.data);
    }
  }

  return truncateText(payload);
}

ChartPoint extractMetrics(const LogEntry &entry, const ChartPoint &previous) {
  const QString text = entry.format == PayloadFormat::Hex ? hexToAscii(entry.payload) : entry.payload;
  const QJsonObject json = tryParseObject(text);
  QRegularExpression numberPattern("(-?\\d+(?:\\.\\d+)?)");
  QRegularExpressionMatchIterator iterator = numberPattern.globalMatch(text);
  QVector<double> numbers;
  while (iterator.hasNext()) {
    const QRegularExpressionMatch match = iterator.next();
    numbers.append(match.captured(1).toDouble());
  }
  int numberIndex = 0;

  auto pickValue = [&](const QStringList &keys, double fallback) {
    for (const QString &key : keys) {
      const QJsonValue value = json.value(key);
      if (value.isDouble()) {
        return value.toDouble();
      }
    }
    if (numberIndex < numbers.size()) {
      return numbers.at(numberIndex++);
    }
    return fallback;
  };

  ChartPoint point;
  point.id = previous.id + 1;
  point.time = nowStamp();
  point.voltage = clampMetric(pickValue({"voltage", "vbat", "u"}, previous.voltage), 0.0, 60.0);
  point.current = clampMetric(pickValue({"current", "i"}, previous.current), 0.0, 30.0);
  point.temperature = clampMetric(pickValue({"temperature", "temp", "t"}, previous.temperature), -40.0, 180.0);
  point.rpm = clampMetric(pickValue({"rpm", "speed"}, previous.rpm), 0.0, 12000.0);
  return point;
}

QString logsToCsv(const QVector<LogEntry> &logs) {
  QStringList rows;
  rows.append("time,channel,direction,format,bytes,payload,decoded,meta");
  for (const LogEntry &log : logs) {
    const QStringList columns = {
      log.ts,
      channelKeyToString(log.channel),
      directionToString(log.direction),
      payloadFormatToString(log.format),
      QString::number(log.bytes),
      log.payload,
      log.decoded,
      log.meta
    };
    QStringList escaped;
    escaped.reserve(columns.size());
    for (const QString &column : columns) {
      QString value = column;
      value.replace("\"", "\"\"");
      escaped.append(QString("\"%1\"").arg(value));
    }
    rows.append(escaped.join(','));
  }
  return rows.join('\n');
}

QString buildHtmlReport(const QVector<LogEntry> &logs, const QVector<ChartPoint> &chartPoints) {
  QMap<QString, int> byChannel;
  for (const LogEntry &log : logs) {
    byChannel[channelKeyToString(log.channel)] += 1;
  }

  const ChartPoint latest = chartPoints.isEmpty() ? ChartPoint{} : chartPoints.last();
  QString rows;
  const int limit = qMin(logs.size(), 120);
  for (int index = 0; index < limit; ++index) {
    const LogEntry &log = logs.at(index);
    rows += QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
      .arg(escapeHtml(log.ts))
      .arg(escapeHtml(channelKeyToString(log.channel)))
      .arg(escapeHtml(directionToString(log.direction)))
      .arg(escapeHtml(payloadFormatToString(log.format)))
      .arg(escapeHtml(log.payload))
      .arg(escapeHtml(log.decoded));
  }

  QStringList channelStats;
  for (auto it = byChannel.begin(); it != byChannel.end(); ++it) {
    channelStats.append(QString("%1: %2").arg(it.key()).arg(it.value()));
  }

  return QString(R"(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <title>嵌入式调试报告</title>
  <style>
    body{font-family:"Noto Sans SC","Microsoft YaHei",sans-serif;margin:32px;color:#1c2430;background:#f7f9fc}
    h1{font-size:24px;margin:0 0 8px}
    .meta{color:#627084;margin-bottom:24px}
    .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin-bottom:20px}
    .card{background:#fff;border:1px solid #dce3ee;border-radius:8px;padding:14px}
    .label{font-size:12px;color:#627084}
    .value{font-size:22px;font-weight:700;margin-top:6px}
    table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #dce3ee}
    th,td{border-bottom:1px solid #e7edf5;padding:8px;text-align:left;font-size:12px;vertical-align:top}
    th{background:#eef3f9}
  </style>
</head>
<body>
  <h1>嵌入式调试报告</h1>
  <div class="meta">生成时间：%1</div>
  <div class="grid">
    <div class="card"><div class="label">日志条数</div><div class="value">%2</div></div>
    <div class="card"><div class="label">通道数</div><div class="value">%3</div></div>
    <div class="card"><div class="label">最新温度</div><div class="value">%4 C</div></div>
    <div class="card"><div class="label">最新电压</div><div class="value">%5 V</div></div>
  </div>
  <h2>通道统计</h2>
  <p>%6</p>
  <h2>日志明细</h2>
  <table>
    <thead><tr><th>时间</th><th>通道</th><th>方向</th><th>格式</th><th>载荷</th><th>解析</th></tr></thead>
    <tbody>%7</tbody>
  </table>
</body>
</html>)")
    .arg(escapeHtml(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")))
    .arg(logs.size())
    .arg(byChannel.size())
    .arg(QString::number(latest.temperature, 'f', 1))
    .arg(QString::number(latest.voltage, 'f', 2))
    .arg(escapeHtml(channelStats.isEmpty() ? QStringLiteral("无数据") : channelStats.join(" | ")))
    .arg(rows);
}

PayloadFormat inferFormat(const QString &payload) {
  const QString trimmed = payload.trimmed();
  if (trimmed.isEmpty()) {
    return PayloadFormat::Text;
  }
  if ((trimmed.startsWith('{') && trimmed.endsWith('}')) || (trimmed.startsWith('[') && trimmed.endsWith(']'))) {
    return PayloadFormat::Json;
  }
  static const QRegularExpression hexPattern("^(0x)?[0-9A-Fa-f]{2}([\\s,:-]+(0x)?[0-9A-Fa-f]{2})*$");
  if (hexPattern.match(trimmed).hasMatch()) {
    return PayloadFormat::Hex;
  }
  return PayloadFormat::Text;
}
