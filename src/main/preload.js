const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('comtradeScope', {
  getBackendInfo: () => ipcRenderer.invoke('backend:info'),
  startBackend: () => ipcRenderer.invoke('backend:start'),
  openComtradeFile: () => ipcRenderer.invoke('file:openComtrade'),
  saveTextFile: (options) => ipcRenderer.invoke('file:saveText', options),
  onBackendLog: (callback) => {
    const handler = (_event, message) => callback(message);
    ipcRenderer.on('backend:log', handler);
    return () => ipcRenderer.removeListener('backend:log', handler);
  },
  onBackendExit: (callback) => {
    const handler = (_event, code) => callback(code);
    ipcRenderer.on('backend:exit', handler);
    return () => ipcRenderer.removeListener('backend:exit', handler);
  }
});
