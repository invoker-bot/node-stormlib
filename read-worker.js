'use strict'

const { parentPort, workerData } = require('worker_threads')

const native = require('./native')

try {
  const buffer = native.readFile(
    workerData.archivePath,
    workerData.fileName,
    workerData.options,
  )
  parentPort.postMessage({ buffer })
} catch (error) {
  parentPort.postMessage({
    error: {
      name: error.name,
      message: error.message,
      code: error.code,
      stormCode: error.stormCode,
      stack: error.stack,
    },
  })
}
