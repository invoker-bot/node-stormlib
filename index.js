'use strict'

const path = require('path')
const { Readable } = require('stream')
const { Worker } = require('worker_threads')

const native = require('./native')

function assertOptions(options) {
  if (options === undefined || options === null) {
    return {}
  }

  if (typeof options !== 'object' || Array.isArray(options)) {
    throw new TypeError('options must be an object')
  }

  return options
}

function assertInsideRoot(outputPath, rootDir) {
  const resolvedOutputPath = path.resolve(outputPath)
  const resolvedRootDir = path.resolve(rootDir)
  const relative = path.relative(resolvedRootDir, resolvedOutputPath)

  if (relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative))) {
    return
  }

  const error = new RangeError('outputPath must stay inside the configured rootDir')
  error.code = 'ERR_NODE_STORM_PATH_OUTSIDE_ROOT'
  throw error
}

function deserializeWorkerError(serialized) {
  const error = new Error(serialized.message)
  error.name = serialized.name || 'Error'
  error.stack = serialized.stack

  if (serialized.code) {
    error.code = serialized.code
  }
  if (serialized.stormCode) {
    error.stormCode = serialized.stormCode
  }

  return error
}

function getArchiveInfo(archivePath) {
  return native.getArchiveInfo(archivePath)
}

function listFiles(archivePath, mask, options) {
  if (mask !== null && typeof mask === 'object' && !Array.isArray(mask)) {
    return native.listFiles(archivePath, mask)
  }

  return native.listFiles(archivePath, mask, options)
}

function hasFile(archivePath, fileName) {
  return native.hasFile(archivePath, fileName)
}

function readFile(archivePath, fileName, options) {
  return native.readFile(archivePath, fileName, options)
}

function extractFile(archivePath, fileName, outputPath, options) {
  const normalizedOptions = assertOptions(options)

  if (normalizedOptions.rootDir !== undefined && normalizedOptions.rootDir !== null) {
    if (typeof normalizedOptions.rootDir !== 'string') {
      throw new TypeError('rootDir must be a string')
    }
    assertInsideRoot(outputPath, normalizedOptions.rootDir)
  }

  return native.extractFile(archivePath, fileName, outputPath)
}

function readFileAsync(archivePath, fileName, options) {
  return new Promise((resolve, reject) => {
    const worker = new Worker(path.join(__dirname, 'read-worker.js'), {
      workerData: {
        archivePath,
        fileName,
        options,
      },
    })
    let settled = false

    worker.once('message', message => {
      settled = true
      if (message.error) {
        reject(deserializeWorkerError(message.error))
        return
      }

      resolve(Buffer.from(message.buffer))
    })

    worker.once('error', error => {
      settled = true
      reject(error)
    })

    worker.once('exit', code => {
      if (!settled && code !== 0) {
        reject(new Error(`read worker exited with code ${code}`))
      }
    })
  })
}

function createReadStream(archivePath, fileName, options) {
  let started = false

  return new Readable({
    read() {
      if (started) {
        return
      }

      started = true
      readFileAsync(archivePath, fileName, options)
        .then(buffer => {
          this.push(buffer)
          this.push(null)
        })
        .catch(error => {
          this.destroy(error)
        })
    },
  })
}

module.exports = {
  getArchiveInfo,
  listFiles,
  hasFile,
  readFile,
  readFileAsync,
  createReadStream,
  extractFile,
}
