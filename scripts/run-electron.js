const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const root = path.join(__dirname, '..');

function findElectron() {
  const suffix = process.platform === 'win32'
    ? ['dist', 'electron.exe']
    : ['dist', 'electron'];
  const candidates = [
    path.join(root, 'node_modules', 'electron', ...suffix),
    path.join(root, '..', 'node_modules', 'electron', ...suffix)
  ];
  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }
  try {
    return require('electron');
  } catch {
    throw new Error('未找到 Electron。请先执行 npm install，或在上层目录安装 electron。');
  }
}

const electron = findElectron();
const args = [root, ...process.argv.slice(2)];
const child = spawn(electron, args, {
  cwd: root,
  stdio: 'inherit',
  env: {
    ...process.env,
    ELECTRON_DEV: process.argv.includes('--dev') ? '1' : process.env.ELECTRON_DEV
  }
});

child.on('exit', (code) => {
  process.exit(code ?? 0);
});
