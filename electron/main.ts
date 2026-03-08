import { app, BrowserWindow, ipcMain, dialog } from 'electron'
import path from 'node:path'
import fs from 'node:fs/promises'
import * as NativeBridge from './native-bridge'

let win: BrowserWindow | null = null
const isMac = process.platform === 'darwin'

function createWindow() {
  win = new BrowserWindow({
    width: 1200,
    height: 800,
    backgroundColor: '#0a0a0a',
    titleBarStyle: isMac ? 'hiddenInset' : 'hidden',
    ...(isMac && { trafficLightPosition: { x: 10, y: 10 } }),
    webPreferences: {
      preload: path.join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  })

  if (process.env.ELECTRON_RENDERER_URL) {
    win.loadURL(process.env.ELECTRON_RENDERER_URL)
    win.webContents.openDevTools()
  } else {
    win.loadFile(path.join(__dirname, '../renderer/index.html'))
  }
}

app.whenReady().then(() => {
  createWindow()
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => app.quit())

ipcMain.handle('open-file-dialog', async () => {
  const result = await dialog.showOpenDialog({
    properties: ['openFile'],
    filters: [
      { name: 'STEP', extensions: ['stp', 'step'] },
      { name: 'All', extensions: ['*'] },
    ],
  })
  if (result.canceled) return []
  return result.filePaths
})

ipcMain.handle('show-save-dialog', async (_event, options: { defaultPath?: string; filters?: Array<{ name: string; extensions: string[] }> }) => {
  const result = await dialog.showSaveDialog({
    defaultPath: options?.defaultPath,
    filters: options?.filters ?? [{ name: 'STEP', extensions: ['stp'] }],
  })
  if (result.canceled) return null
  return result.filePath
})

ipcMain.handle('write-file', async (_event, filepath: string, content: Buffer) => {
  await fs.writeFile(filepath, content)
  return true
})

ipcMain.handle('analyse-step', async (_event, filepath: string, quality: string = 'fast') => {
  const { BrowserWindow } = await import('electron')
  const w = BrowserWindow.getAllWindows()[0]
  const onLog = (msg: string) => {
    if (w && !w.isDestroyed()) w.webContents.send('backend-log', msg)
  }
  return NativeBridge.analyseStep(filepath, quality, onLog)
})

ipcMain.handle('repair-step', async (
  _event,
  filepath: string,
  outputPath: string,
  options: { fixNames: boolean; fixShells: boolean; fixHoopsCompat: boolean }
) => {
  const { BrowserWindow } = await import('electron')
  const w = BrowserWindow.getAllWindows()[0]
  const onLog = (msg: string) => {
    if (w && !w.isDestroyed()) w.webContents.send('backend-log', msg)
  }
  return NativeBridge.repairStep(filepath, outputPath, options, onLog)
})

ipcMain.handle('load-step-mesh', async (_event, filepath: string, quality: string = 'standard') => {
  return NativeBridge.loadStepMesh(filepath, quality)
})

ipcMain.handle('window-minimize', () => { if (win) win.minimize() })
ipcMain.handle('window-maximize', () => {
  if (win) {
    if (win.isMaximized()) win.unmaximize()
    else win.maximize()
  }
})
ipcMain.handle('window-close', () => { if (win) win.close() })
ipcMain.handle('window-is-maximized', () => (win ? win.isMaximized() : false))
