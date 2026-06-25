const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const WS_PORT = 48010;

let mainWindow = null;
let backendProcess = null;
let isQuitting = false;

function backendPath() {
  if (process.env.COMTRADESCOPE_BACKEND) {
    return process.env.COMTRADESCOPE_BACKEND;
  }
  const exe = process.platform === 'win32' ? 'comtradescope-backend.exe' : 'comtradescope-backend';
  return path.join(app.getAppPath(), 'backend', 'bin', exe);
}

function startBackend() {
  const exe = backendPath();
  if (!fs.existsSync(exe)) {
    return {
      started: false,
      message: `未找到 Native C++ 后端：${exe}`
    };
  }

  if (backendProcess) {
    return { started: true, message: 'Native C++ 后端已运行' };
  }

  backendProcess = spawn(exe, ['--port', String(WS_PORT)], {
    cwd: path.dirname(exe),
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe']
  });

  backendProcess.stdout.on('data', (chunk) => {
    mainWindow?.webContents.send('backend:log', chunk.toString('utf8'));
  });

  backendProcess.stderr.on('data', (chunk) => {
    mainWindow?.webContents.send('backend:log', chunk.toString('utf8'));
  });

  backendProcess.on('exit', (code) => {
    backendProcess = null;
    mainWindow?.webContents.send('backend:exit', code);
    if (!isQuitting) {
      mainWindow?.webContents.send('backend:log', `Native C++ 后端已退出，代码 ${code ?? 'unknown'}`);
    }
  });

  return { started: true, message: `Native C++ 后端已启动：${exe}` };
}

function stopBackend() {
  isQuitting = true;
  if (backendProcess) {
    backendProcess.kill();
    backendProcess = null;
  }
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1560,
    height: 960,
    minWidth: 1180,
    minHeight: 760,
    backgroundColor: '#0f1115',
    title: 'ComtradeScope',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false
    }
  });

  mainWindow.loadFile(path.join(__dirname, '..', 'renderer', 'index.html'));

  if (process.env.ELECTRON_DEV === '1') {
    mainWindow.webContents.openDevTools({ mode: 'detach' });
  }
}

ipcMain.handle('backend:info', () => ({
  wsUrl: `ws://127.0.0.1:${WS_PORT}`,
  backendPath: backendPath(),
  appPath: app.getAppPath(),
  demoCfgPath: path.join(app.getAppPath(), 'samples', 'demo_fault.cfg'),
  demoDatPath: path.join(app.getAppPath(), 'samples', 'demo_fault.dat')
}));

ipcMain.handle('backend:start', () => startBackend());

ipcMain.handle('file:openComtrade', async () => {
  const cfgResult = await dialog.showOpenDialog(mainWindow, {
    title: '选择 COMTRADE CFG 文件',
    filters: [{ name: 'COMTRADE CFG', extensions: ['cfg', 'CFG'] }],
    properties: ['openFile']
  });

  if (cfgResult.canceled || cfgResult.filePaths.length === 0) {
    return { ok: false, canceled: true, message: '已取消打开' };
  }

  const cfgPath = cfgResult.filePaths[0];
  const base = cfgPath.slice(0, -path.extname(cfgPath).length);
  const datCandidates = [`${base}.dat`, `${base}.DAT`];
  let datPath = datCandidates.find((candidate) => fs.existsSync(candidate)) || '';

  if (!datPath) {
    const datResult = await dialog.showOpenDialog(mainWindow, {
      title: '选择对应的 COMTRADE DAT 文件',
      defaultPath: path.dirname(cfgPath),
      filters: [{ name: 'COMTRADE DAT', extensions: ['dat', 'DAT'] }],
      properties: ['openFile']
    });
    if (!datResult.canceled && datResult.filePaths.length > 0) {
      datPath = datResult.filePaths[0];
    }
  }

  return { ok: true, canceled: false, cfgPath, datPath };
});

ipcMain.handle('file:saveText', async (_event, options = {}) => {
  const result = await dialog.showSaveDialog(mainWindow, {
    title: options.title || '保存文件',
    defaultPath: options.defaultPath || 'comtradescope.txt',
    filters: options.filters || [{ name: 'Text', extensions: ['txt'] }],
    properties: ['createDirectory']
  });

  if (result.canceled || !result.filePath) {
    return { ok: false, canceled: true, message: '已取消保存' };
  }

  await fs.promises.writeFile(result.filePath, String(options.content || ''), 'utf8');
  return { ok: true, canceled: false, filePath: result.filePath, message: '文件已保存' };
});

app.whenReady().then(() => {
  startBackend();
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('before-quit', () => {
  stopBackend();
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
