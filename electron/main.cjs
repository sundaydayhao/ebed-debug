const { app, BrowserWindow, dialog, ipcMain, session } = require("electron");
const fs = require("node:fs/promises");
const path = require("node:path");
const net = require("node:net");
const dgram = require("node:dgram");

let mqtt = null;
try {
  mqtt = require("mqtt");
} catch {
  mqtt = null;
}

let SerialPort = null;
try {
  const serialport = require("serialport");
  SerialPort = serialport.SerialPort || serialport;
} catch {
  SerialPort = null;
}

const connections = new Map();
let mainWindow = null;

function sendToRenderer(payload) {
  if (!mainWindow || mainWindow.isDestroyed()) return;
  mainWindow.webContents.send("native:event", {
    at: new Date().toISOString(),
    ...payload
  });
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1440,
    height: 920,
    minWidth: 1120,
    minHeight: 720,
    title: "嵌入式调试上位机 Pro",
    backgroundColor: "#f5f7fb",
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false
    }
  });

  if (!app.isPackaged) {
    mainWindow.loadURL("http://localhost:5173");
    mainWindow.webContents.openDevTools({ mode: "detach" });
  } else {
    mainWindow.loadFile(path.join(__dirname, "..", "dist", "index.html"));
  }
}

function closeConnection(id) {
  const item = connections.get(id);
  if (!item) return;

  try {
    if (item.kind === "tcp") item.client.destroy();
    if (item.kind === "udp") item.client.close();
    if (item.kind === "mqtt") item.client.end(true);
    if (item.kind === "serial") item.client.close();
  } catch (error) {
    sendToRenderer({
      id,
      type: "status",
      level: "error",
      message: error instanceof Error ? error.message : String(error)
    });
  } finally {
    connections.delete(id);
  }
}

function normalizeBuffer(payload, format) {
  if (format === "hex") {
    const clean = String(payload).replace(/0x/gi, "").replace(/[^a-fA-F0-9]/g, "");
    if (clean.length % 2 !== 0) {
      throw new Error("HEX payload length must be even.");
    }
    return Buffer.from(clean, "hex");
  }

  if (typeof payload === "object") {
    return Buffer.from(JSON.stringify(payload));
  }

  return Buffer.from(String(payload));
}

function formatSerialError(error, devicePath) {
  const message = error instanceof Error ? error.message : String(error);
  if (/EACCES|EPERM|permission|access|权限|cannot open/i.test(message)) {
    return `串口权限不足，无法打开 ${devicePath}。Linux 下请执行 sudo usermod -aG dialout $USER 后注销并重新登录；临时测试可执行 sudo chmod a+rw ${devicePath}。原始错误：${message}`;
  }
  return message;
}

app.whenReady().then(() => {
  session.defaultSession.setPermissionCheckHandler((_webContents, permission) => {
    if (permission === "serial") return true;
    return false;
  });

  createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  for (const id of connections.keys()) closeConnection(id);
  if (process.platform !== "darwin") app.quit();
});

ipcMain.handle("native:capabilities", async () => {
  return {
    platform: process.platform,
    serialAvailable: Boolean(SerialPort),
    tcpAvailable: true,
    udpAvailable: true,
    mqttAvailable: Boolean(mqtt),
    node: process.versions.node,
    electron: process.versions.electron
  };
});

ipcMain.handle("serial:list", async () => {
  if (!SerialPort) return [];
  return SerialPort.list();
});

ipcMain.handle("connection:open", async (_event, request) => {
  const { id, kind, config } = request;
  closeConnection(id);

  if (kind === "tcp") {
    const client = new net.Socket();
    connections.set(id, { kind, client, config });
    client.on("connect", () => sendToRenderer({ id, kind, type: "status", level: "ok", message: "TCP connected" }));
    client.on("data", (data) => sendToRenderer({ id, kind, type: "data", direction: "rx", payload: data.toString("hex"), format: "hex" }));
    client.on("error", (error) => sendToRenderer({ id, kind, type: "status", level: "error", message: error.message }));
    client.on("close", () => sendToRenderer({ id, kind, type: "status", level: "warn", message: "TCP closed" }));
    client.connect(Number(config.port), String(config.host));
    return { ok: true, id };
  }

  if (kind === "udp") {
    const client = dgram.createSocket("udp4");
    connections.set(id, { kind, client, config });
    client.on("message", (data, remote) => {
      sendToRenderer({
        id,
        kind,
        type: "data",
        direction: "rx",
        payload: data.toString("hex"),
        format: "hex",
        meta: `${remote.address}:${remote.port}`
      });
    });
    client.on("error", (error) => sendToRenderer({ id, kind, type: "status", level: "error", message: error.message }));
    client.bind(Number(config.localPort || 0), () => {
      const address = client.address();
      sendToRenderer({ id, kind, type: "status", level: "ok", message: `UDP listening ${address.address}:${address.port}` });
    });
    return { ok: true, id };
  }

  if (kind === "mqtt") {
    if (!mqtt) throw new Error("MQTT package is not installed.");
    const client = mqtt.connect(String(config.url), {
      clientId: config.clientId || `ebed-debug-${Date.now()}`,
      username: config.username || undefined,
      password: config.password || undefined,
      clean: true,
      reconnectPeriod: 2000
    });
    connections.set(id, { kind, client, config });
    client.on("connect", () => {
      sendToRenderer({ id, kind, type: "status", level: "ok", message: "MQTT connected" });
      if (config.subscribeTopic) client.subscribe(String(config.subscribeTopic));
    });
    client.on("message", (topic, data) => {
      sendToRenderer({ id, kind, type: "data", direction: "rx", payload: data.toString(), format: "text", meta: topic });
    });
    client.on("error", (error) => sendToRenderer({ id, kind, type: "status", level: "error", message: error.message }));
    client.on("close", () => sendToRenderer({ id, kind, type: "status", level: "warn", message: "MQTT closed" }));
    return { ok: true, id };
  }

  if (kind === "serial") {
    if (!SerialPort) throw new Error("serialport package is not available in this runtime.");
    const devicePath = String(config.path);
    const client = new SerialPort({
      path: devicePath,
      baudRate: Number(config.baudRate || 115200),
      dataBits: Number(config.dataBits || 8),
      stopBits: Number(config.stopBits || 1),
      parity: config.parity || "none",
      autoOpen: false
    });
    connections.set(id, { kind, client, config });
    client.on("open", () => sendToRenderer({ id, kind, type: "status", level: "ok", message: "Serial opened" }));
    client.on("data", (data) => sendToRenderer({ id, kind, type: "data", direction: "rx", payload: data.toString("hex"), format: "hex" }));
    client.on("error", (error) => sendToRenderer({ id, kind, type: "status", level: "error", message: error.message }));
    client.on("close", () => sendToRenderer({ id, kind, type: "status", level: "warn", message: "Serial closed" }));
    await new Promise((resolve, reject) => client.open((error) => (error ? reject(new Error(formatSerialError(error, devicePath))) : resolve())));
    return { ok: true, id };
  }

  throw new Error(`Unsupported connection kind: ${kind}`);
});

ipcMain.handle("connection:send", async (_event, request) => {
  const { id, payload, format, meta } = request;
  const item = connections.get(id);
  if (!item) throw new Error("Connection is not open.");

  if (item.kind === "mqtt") {
    item.client.publish(String(meta?.topic || item.config.publishTopic || item.config.subscribeTopic || "debug/tx"), String(payload));
    return { ok: true };
  }

  const buffer = normalizeBuffer(payload, format);

  if (item.kind === "tcp" || item.kind === "serial") {
    item.client.write(buffer);
    return { ok: true, bytes: buffer.length };
  }

  if (item.kind === "udp") {
    const host = String(meta?.host || item.config.remoteHost || item.config.host || "127.0.0.1");
    const port = Number(meta?.port || item.config.remotePort || item.config.port);
    await new Promise((resolve, reject) => {
      item.client.send(buffer, port, host, (error) => (error ? reject(error) : resolve()));
    });
    return { ok: true, bytes: buffer.length };
  }

  throw new Error("Unsupported connection kind.");
});

ipcMain.handle("connection:close", async (_event, id) => {
  closeConnection(id);
  return { ok: true };
});

ipcMain.handle("file:save", async (_event, request) => {
  const result = await dialog.showSaveDialog(mainWindow, {
    title: request.title || "保存文件",
    defaultPath: request.defaultPath || "ebed-debug-export.txt",
    filters: request.filters || [{ name: "All files", extensions: ["*"] }]
  });

  if (result.canceled || !result.filePath) {
    return { ok: false, canceled: true };
  }

  await fs.writeFile(result.filePath, request.content, request.encoding || "utf8");
  return { ok: true, path: result.filePath };
});
