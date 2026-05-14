import type {
  CanFrame,
  CanSignal,
  ChannelKey,
  ChartPoint,
  LogEntry,
  ModbusBuilderState,
  PayloadFormat
} from "../types";

export function nowStamp(date = new Date()) {
  return date.toLocaleTimeString("zh-CN", { hour12: false });
}

export function byteLength(payload: string, format: PayloadFormat | "event") {
  if (format === "hex") {
    return Math.floor(payload.replace(/0x/gi, "").replace(/[^a-fA-F0-9]/g, "").length / 2);
  }
  return new TextEncoder().encode(payload).length;
}

export function normalizeHex(value: string) {
  return value
    .replace(/0x/gi, "")
    .replace(/[^a-fA-F0-9]/g, "")
    .replace(/(.{2})/g, "$1 ")
    .trim()
    .toUpperCase();
}

export function hexToBytes(value: string) {
  const clean = value.replace(/0x/gi, "").replace(/[^a-fA-F0-9]/g, "");
  const bytes: number[] = [];
  for (let index = 0; index < clean.length; index += 2) {
    bytes.push(Number.parseInt(clean.slice(index, index + 2), 16));
  }
  return bytes.filter((byte) => Number.isFinite(byte));
}

export function bytesToHex(bytes: number[]) {
  return bytes.map((byte) => byte.toString(16).padStart(2, "0")).join(" ").toUpperCase();
}

export function textToHex(value: string) {
  return bytesToHex(Array.from(new TextEncoder().encode(value)));
}

export function hexToAscii(value: string) {
  return hexToBytes(value)
    .map((byte) => (byte >= 32 && byte <= 126 ? String.fromCharCode(byte) : "."))
    .join("");
}

export function crc16Modbus(bytes: number[]) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) {
      const lsb = crc & 1;
      crc >>= 1;
      if (lsb) crc ^= 0xa001;
    }
  }
  return crc & 0xffff;
}

function pushWord(bytes: number[], value: number) {
  bytes.push((value >> 8) & 0xff, value & 0xff);
}

export function buildModbusFrame(state: ModbusBuilderState) {
  const pdu: number[] = [state.functionCode];
  pushWord(pdu, state.address);

  if (state.functionCode === 6) {
    pushWord(pdu, state.value);
  } else if (state.functionCode === 16) {
    pushWord(pdu, state.quantity);
    pdu.push(state.quantity * 2);
    for (let index = 0; index < state.quantity; index += 1) {
      pushWord(pdu, state.value + index);
    }
  } else {
    pushWord(pdu, state.quantity);
  }

  if (state.variant === "tcp") {
    const length = pdu.length + 1;
    const frame = [
      (state.transactionId >> 8) & 0xff,
      state.transactionId & 0xff,
      0x00,
      0x00,
      (length >> 8) & 0xff,
      length & 0xff,
      state.unitId,
      ...pdu
    ];
    return bytesToHex(frame);
  }

  const frame = [state.unitId, ...pdu];
  const crc = crc16Modbus(frame);
  frame.push(crc & 0xff, (crc >> 8) & 0xff);
  return bytesToHex(frame);
}

export function decodeModbusFrame(frame: string) {
  const bytes = hexToBytes(frame);
  if (bytes.length < 5) return "Modbus: 帧长度不足";

  const unit = bytes[0];
  const fn = bytes[1];
  const hasCrc = bytes.length >= 6;
  const crcText = hasCrc
    ? crc16Modbus(bytes.slice(0, -2)) === ((bytes.at(-1) || 0) << 8) + (bytes.at(-2) || 0)
      ? "CRC OK"
      : "CRC ERR"
    : "无 CRC";

  if (fn & 0x80) {
    return `Modbus RTU: Unit ${unit}, 异常功能码 0x${fn.toString(16)}, code ${bytes[2] ?? 0}, ${crcText}`;
  }

  if (fn === 3 || fn === 4) {
    const byteCount = bytes[2] ?? 0;
    const registers: number[] = [];
    for (let index = 3; index < 3 + byteCount; index += 2) {
      if (bytes[index + 1] === undefined) break;
      registers.push((bytes[index] << 8) + bytes[index + 1]);
    }
    return `Modbus RTU: Unit ${unit}, FC${fn}, ${registers.length} registers [${registers.join(", ")}], ${crcText}`;
  }

  return `Modbus RTU: Unit ${unit}, FC${fn}, ${bytes.length} bytes, ${crcText}`;
}

export function parseCanFrameText(input: string): CanFrame | null {
  const candump = input.trim().match(/^([0-9A-Fa-f]{3,8})#([0-9A-Fa-f]*)$/);
  if (candump) {
    const data = candump[2].replace(/(.{2})/g, "$1 ").trim();
    return {
      id: candump[1].toUpperCase(),
      dlc: Math.min(8, Math.ceil(candump[2].length / 2)),
      data,
      extended: candump[1].length > 3
    };
  }

  const parts = input.trim().split(/\s+/);
  if (parts.length >= 2) {
    const id = parts[0].replace(/^0x/i, "");
    const data = parts.slice(1).join(" ");
    return {
      id: id.toUpperCase(),
      dlc: hexToBytes(data).length,
      data: normalizeHex(data),
      extended: id.length > 3
    };
  }

  return null;
}

function extractUnsignedLittleEndian(bytes: number[], startBit: number, length: number) {
  let raw = 0;
  for (let bit = 0; bit < length; bit += 1) {
    const absolute = startBit + bit;
    const byteIndex = Math.floor(absolute / 8);
    const bitIndex = absolute % 8;
    if ((bytes[byteIndex] >> bitIndex) & 1) raw |= 1 << bit;
  }
  return raw;
}

export function decodeCanSignals(frame: CanFrame, signals: CanSignal[]) {
  const bytes = hexToBytes(frame.data);
  return signals.map((signal) => {
    const raw = extractUnsignedLittleEndian(bytes, signal.startBit, signal.length);
    const value = raw * signal.scale + signal.offset;
    return {
      ...signal,
      raw,
      value
    };
  });
}

export function decodePayload(channel: ChannelKey, payload: string, format: PayloadFormat | "event") {
  if (format === "hex") {
    if (channel === "modbus") return decodeModbusFrame(payload);
    return `HEX ${normalizeHex(payload)} | ASCII "${hexToAscii(payload)}"`;
  }

  if (format === "json") {
    try {
      const parsed = JSON.parse(payload) as Record<string, unknown>;
      return Object.entries(parsed)
        .slice(0, 6)
        .map(([key, value]) => `${key}: ${String(value)}`)
        .join(", ");
    } catch {
      return "JSON 解析失败";
    }
  }

  if (channel === "can") {
    const frame = parseCanFrameText(payload);
    if (frame) return `CAN ${frame.id}, DLC ${frame.dlc}, DATA ${frame.data}`;
  }

  return payload.length > 96 ? `${payload.slice(0, 96)}...` : payload;
}

export function extractMetrics(entry: LogEntry, previous: ChartPoint): ChartPoint {
  const text = entry.format === "hex" ? hexToAscii(entry.payload) : entry.payload;
  const numbers = Array.from(text.matchAll(/(-?\d+(?:\.\d+)?)/g)).map((match) => Number(match[1]));
  const json = tryParseJson(text);
  const pick = (keys: string[], fallback: number) => {
    if (json) {
      for (const key of keys) {
        const value = json[key];
        if (typeof value === "number" && Number.isFinite(value)) return value;
      }
    }
    const value = numbers.shift();
    return typeof value === "number" && Number.isFinite(value) ? value : fallback;
  };

  return {
    id: previous.id + 1,
    time: nowStamp(),
    voltage: clampMetric(pick(["voltage", "vbat", "u"], previous.voltage), 0, 60),
    current: clampMetric(pick(["current", "i"], previous.current), 0, 30),
    temperature: clampMetric(pick(["temperature", "temp", "t"], previous.temperature), -40, 180),
    rpm: clampMetric(pick(["rpm", "speed"], previous.rpm), 0, 12000)
  };
}

function tryParseJson(text: string) {
  try {
    return JSON.parse(text) as Record<string, unknown>;
  } catch {
    return null;
  }
}

function clampMetric(value: number, min: number, max: number) {
  if (!Number.isFinite(value)) return min;
  return Math.min(max, Math.max(min, Number(value.toFixed(2))));
}

export function createDemoPayload(channel: ChannelKey, tick: number) {
  const voltage = 3.3 + Math.sin(tick / 4) * 0.18;
  const current = 0.42 + Math.cos(tick / 6) * 0.08;
  const temperature = 34 + Math.sin(tick / 9) * 4;
  const rpm = 1200 + Math.round(Math.sin(tick / 5) * 180);

  if (channel === "serial") {
    return {
      payload: `ADC:${Math.round(voltage * 1000)}mV TEMP:${temperature.toFixed(1)}C I:${current.toFixed(2)}A`,
      format: "text" as PayloadFormat
    };
  }

  if (channel === "tcp" || channel === "udp" || channel === "mqtt") {
    return {
      payload: JSON.stringify({
        voltage: Number(voltage.toFixed(2)),
        current: Number(current.toFixed(2)),
        temperature: Number(temperature.toFixed(1)),
        rpm
      }),
      format: "json" as PayloadFormat
    };
  }

  if (channel === "modbus") {
    const registers = [Math.round(voltage * 100), Math.round(current * 1000)];
    const base = [0x01, 0x03, 0x04, (registers[0] >> 8) & 0xff, registers[0] & 0xff, (registers[1] >> 8) & 0xff, registers[1] & 0xff];
    const crc = crc16Modbus(base);
    return {
      payload: bytesToHex([...base, crc & 0xff, (crc >> 8) & 0xff]),
      format: "hex" as PayloadFormat
    };
  }

  return {
    payload: `18FF50E5#${bytesToHex([Math.round(voltage * 10), Math.round(current * 100), Math.round(temperature), rpm & 0xff, (rpm >> 8) & 0xff, 0, 0, tick & 0xff]).replace(/\s/g, "")}`,
    format: "text" as PayloadFormat
  };
}

export function logsToCsv(logs: LogEntry[]) {
  const header = ["time", "channel", "direction", "format", "bytes", "payload", "decoded", "meta"];
  const rows = logs.map((log) =>
    [log.ts, log.channel, log.direction, log.format, log.bytes ?? "", log.payload, log.decoded ?? "", log.meta ?? ""]
      .map((value) => `"${String(value).replace(/"/g, '""')}"`)
      .join(",")
  );
  return [header.join(","), ...rows].join("\n");
}

export function buildHtmlReport(logs: LogEntry[], chartPoints: ChartPoint[]) {
  const byChannel = logs.reduce<Record<string, number>>((acc, log) => {
    acc[log.channel] = (acc[log.channel] || 0) + 1;
    return acc;
  }, {});

  const latest = chartPoints.at(-1);
  const rows = logs
    .slice(0, 120)
    .map(
      (log) =>
        `<tr><td>${escapeHtml(log.ts)}</td><td>${escapeHtml(log.channel)}</td><td>${escapeHtml(log.direction)}</td><td>${escapeHtml(log.format)}</td><td>${escapeHtml(log.payload)}</td><td>${escapeHtml(log.decoded || "")}</td></tr>`
    )
    .join("");

  return `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <title>嵌入式调试报告</title>
  <style>
    body{font-family:Arial,"Microsoft YaHei",sans-serif;margin:32px;color:#1c2430;background:#f7f9fc}
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
  <div class="meta">生成时间：${escapeHtml(new Date().toLocaleString("zh-CN"))}</div>
  <div class="grid">
    <div class="card"><div class="label">日志条数</div><div class="value">${logs.length}</div></div>
    <div class="card"><div class="label">通道数</div><div class="value">${Object.keys(byChannel).length}</div></div>
    <div class="card"><div class="label">最新温度</div><div class="value">${latest ? latest.temperature.toFixed(1) : "-"} C</div></div>
    <div class="card"><div class="label">最新电压</div><div class="value">${latest ? latest.voltage.toFixed(2) : "-"} V</div></div>
  </div>
  <h2>通道统计</h2>
  <p>${escapeHtml(Object.entries(byChannel).map(([key, value]) => `${key}: ${value}`).join(" | ") || "无数据")}</p>
  <h2>日志明细</h2>
  <table>
    <thead><tr><th>时间</th><th>通道</th><th>方向</th><th>格式</th><th>载荷</th><th>解析</th></tr></thead>
    <tbody>${rows}</tbody>
  </table>
</body>
</html>`;
}

function escapeHtml(value: string) {
  return value
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}
