const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("desktopAPI", {
  capabilities: () => ipcRenderer.invoke("native:capabilities"),
  listSerialPorts: () => ipcRenderer.invoke("serial:list"),
  openConnection: (request) => ipcRenderer.invoke("connection:open", request),
  sendConnection: (request) => ipcRenderer.invoke("connection:send", request),
  closeConnection: (id) => ipcRenderer.invoke("connection:close", id),
  saveFile: (request) => ipcRenderer.invoke("file:save", request),
  onNativeEvent: (callback) => {
    const listener = (_event, payload) => callback(payload);
    ipcRenderer.on("native:event", listener);
    return () => ipcRenderer.removeListener("native:event", listener);
  }
});
