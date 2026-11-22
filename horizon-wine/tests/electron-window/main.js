'use strict'

const fs = require('fs')
const path = require('path')
const { app, BrowserWindow, ipcMain } = require('electron')

const directory = path.dirname(process.execPath)
const run = Date.now() + '-' + process.pid
const challenge = require('crypto').randomBytes(16).toString('hex')
let win
let echoPid = 0
let frameFile
let complete = false
function checkpoint() {
  if (!complete && frameFile && echoPid) {
    complete = true
    record('probe-passed', { rendererWindowsPid: echoPid, frameFile })
    if (process.argv.includes('--probe-exit-on-pass')) app.quit()
  }
}

function record(event, fields = {}) {
  const line = JSON.stringify({ run, time: new Date().toISOString(), event, ...fields })
  try {
    fs.appendFileSync(path.join(directory, 'probe.log'), line + '\n')
  } catch (error) {
    console.error('Writing probe.log failed:', error.message)
  }
  console.log(line)
}

record('main-entered', { pid: process.pid, electron: process.versions.electron,
  chrome: process.versions.chrome, arch: process.arch, platform: process.platform })

// Sample well inside each color field and away from text. Green and magenta
// have identical component values in RGB and BGR order.
function matchesTestPage(image) {
  if (image.isEmpty()) return false
  const { width, height } = image.getSize()
  const data = image.toBitmap({ scaleFactor: 1 })
  if (width < 4 || height < 4 || data.length !== width * height * 4) return false
  const pixel = fraction => 4 * (Math.floor(height * 0.75) * width + Math.floor(width * fraction))
  const left = pixel(0.25)
  const right = pixel(0.75)
  // Display profiles change the exact channel values. Require two opaque,
  // bright samples with opposing green dominance.
  return data[left + 3] > 240 && data[right + 3] > 240 &&
    Math.min(data[left], data[left + 2]) > Math.max(160, data[left + 1] + 100) &&
    data[right + 1] > Math.max(160, data[right] + 100, data[right + 2] + 100)
}

async function verifyFrame(contents) {
  for (let attempt = 1; attempt <= 3 && !contents.isDestroyed(); attempt++) {
    record('capture-started', { attempt })
    try {
      const image = await contents.capturePage()
      const matched = matchesTestPage(image)
      record('capture-result', { attempt, ...image.getSize(), matched })
      if (matched) {
        const file = 'frame-' + run + '.png'
        fs.writeFileSync(path.join(directory, file), image.toPNG())
        record('frame-verified', { file })
        frameFile = file
        checkpoint()
        return
      }
    } catch (error) {
      record('capture-error', { error: error.message })
    }
    await new Promise(resolve => setTimeout(resolve, 500))
  }
}

let captureStarted = false
ipcMain.on('probe-stage', (event, stage, pid) => {
  if (!win || event.sender !== win.webContents) return
  const rendererPid = event.sender.getOSProcessId()
  record(stage, { rendererPid, reportedPid: pid, mainPid: process.pid,
    separateRenderer: rendererPid > 0 && rendererPid === pid && rendererPid !== process.pid })
  if (stage === 'renderer-frame-callback' && !captureStarted) {
    captureStarted = true
    verifyFrame(event.sender).catch(error => record('capture-error', { error: error.message }))
  }
})

ipcMain.handle('probe-challenge', event => {
  if (!win || event.sender !== win.webContents) throw new Error('unexpected webContents')
  record('mojo-challenge-sent', { rendererWindowsPid: event.sender.getOSProcessId() })
  return { run, challenge }
})
ipcMain.on('probe-echo', (event, id, echo, pid) => {
  if (!win || event.sender !== win.webContents) return
  const matched = id === run && echo === challenge && pid === event.sender.getOSProcessId() &&
    pid > 0 && pid !== process.pid
  record('mojo-roundtrip', { matched, rendererWindowsPid: pid, browserWindowsPid: process.pid })
  if (matched) { echoPid = pid; checkpoint() }
})
app.on('will-quit', () => record('probe-exit', { roundtrip: !!echoPid, frame: !!frameFile }))
app.on('child-process-gone', (_event, details) => record('child-process-gone', details))
app.on('gpu-process-crashed', (_event, killed) => record('gpu-process-crashed', { killed }))
app.on('window-all-closed', () => app.quit())
app.whenReady().then(() => {
  record('app-ready')
  record('window-create-started')
  win = new BrowserWindow({ width: 720, height: 420, show: true, autoHideMenuBar: true,
    backgroundColor: '#202020', title: 'CorkDeck Electron 10.1.5',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false } })
  const contents = win.webContents
  record('window-created', { id: win.id, visible: win.isVisible() })
  win.on('ready-to-show', () => record('window-ready-to-show'))
  contents.on('did-finish-load', () => record('page-loaded'))
  contents.on('did-fail-load', (_e, code, description) => record('page-failed', { code, description }))
  contents.on('render-process-gone', (_e, details) => record('renderer-gone', details))
  contents.on('crashed', (_e, killed) => record('renderer-crashed', { killed }))
  return win.loadFile(path.join(__dirname, 'index.html'))
}).catch(error => record('startup-error', { error: error.stack || error.message }))
