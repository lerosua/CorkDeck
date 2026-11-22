'use strict'

const { ipcRenderer } = require('electron')
const report = stage => ipcRenderer.send('probe-stage', stage, process.pid)

report('renderer-preload')
window.addEventListener('DOMContentLoaded', () => {
  report('renderer-dom-ready')
  ipcRenderer.invoke('probe-challenge').then(({ run, challenge }) => {
    ipcRenderer.send('probe-echo', run, challenge, process.pid)
  }).catch(() => report('mojo-challenge-failed'))
  requestAnimationFrame(() => requestAnimationFrame(() => report('renderer-frame-callback')))
})
