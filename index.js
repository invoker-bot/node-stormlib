'use strict'

const fs = require('fs')
const path = require('path')
const { Readable } = require('stream')
const { Worker } = require('worker_threads')

const native = require('./native')

const compression = Object.freeze({
  none: 'none',
  zlib: 'zlib',
  pkware: 'pkware',
  bzip2: 'bzip2',
  sparse: 'sparse',
  lzma: 'lzma',
  implode: 'implode',
})

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
  error.code = 'ERR_NODE_STORMLIB_PATH_OUTSIDE_ROOT'
  throw error
}

function assertBooleanOption(options, name) {
  const value = options[name]
  if (value === undefined || value === null) {
    return
  }

  if (typeof value !== 'boolean') {
    throw new TypeError(`${name} must be a boolean`)
  }
}

function assertArchiveName(archivedName) {
  if (typeof archivedName !== 'string' || archivedName.length === 0) {
    throw new TypeError('archivedName must be a non-empty string')
  }

  if (archivedName.includes('\0')) {
    throw new TypeError('archivedName must not contain null bytes')
  }

  const normalized = archivedName.replace(/\\/g, '/')
  if (path.posix.isAbsolute(normalized) || /^[A-Za-z]:/.test(normalized)) {
    const error = new RangeError('archivedName must be a relative archive path')
    error.code = 'ERR_NODE_STORMLIB_UNSAFE_ARCHIVE_NAME'
    throw error
  }

  const segments = normalized.split('/')
  if (segments.some(segment => segment === '' || segment === '.' || segment === '..')) {
    const error = new RangeError('archivedName must not contain empty or traversal segments')
    error.code = 'ERR_NODE_STORMLIB_UNSAFE_ARCHIVE_NAME'
    throw error
  }
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

function createArchive(archivePath, options) {
  const normalizedOptions = assertOptions(options)

  assertBooleanOption(normalizedOptions, 'overwrite')
  if (normalizedOptions.rootDir !== undefined && normalizedOptions.rootDir !== null) {
    if (typeof normalizedOptions.rootDir !== 'string') {
      throw new TypeError('rootDir must be a string')
    }
    assertInsideRoot(archivePath, normalizedOptions.rootDir)
  }

  if (fs.existsSync(archivePath)) {
    if (!normalizedOptions.overwrite) {
      const error = new Error('archivePath already exists')
      error.code = 'EEXIST'
      throw error
    }

    const stats = fs.statSync(archivePath)
    if (!stats.isFile()) {
      const error = new Error('archivePath exists and is not a file')
      error.code = 'EEXIST'
      throw error
    }

    fs.rmSync(archivePath)
  }

  return native.createArchive(archivePath, normalizedOptions)
}

function addFile(archivePath, sourcePath, archivedName, options) {
  const normalizedOptions = assertOptions(options)

  assertArchiveName(archivedName)
  if (normalizedOptions.rootDir !== undefined && normalizedOptions.rootDir !== null) {
    if (typeof normalizedOptions.rootDir !== 'string') {
      throw new TypeError('rootDir must be a string')
    }
    assertInsideRoot(archivePath, normalizedOptions.rootDir)
  }
  if (normalizedOptions.sourceRootDir !== undefined && normalizedOptions.sourceRootDir !== null) {
    if (typeof normalizedOptions.sourceRootDir !== 'string') {
      throw new TypeError('sourceRootDir must be a string')
    }
    assertInsideRoot(sourcePath, normalizedOptions.sourceRootDir)
  }

  const stats = fs.statSync(sourcePath)
  if (!stats.isFile()) {
    throw new TypeError('sourcePath must be a file')
  }

  if (normalizedOptions.maxBytes !== undefined && normalizedOptions.maxBytes !== null) {
    if (!Number.isSafeInteger(normalizedOptions.maxBytes) || normalizedOptions.maxBytes < 0) {
      throw new RangeError('maxBytes must be a non-negative safe integer')
    }
    if (stats.size > normalizedOptions.maxBytes) {
      const error = new RangeError('source file exceeds maxBytes limit')
      error.code = 'ERR_NODE_STORMLIB_LIMIT'
      throw error
    }
  }

  return native.addFile(archivePath, sourcePath, archivedName, normalizedOptions)
}

function writeFile(archivePath, archivedName, data, options) {
  const normalizedOptions = assertOptions(options)

  assertArchiveName(archivedName)
  if (!Buffer.isBuffer(data)) {
    throw new TypeError('data must be a Buffer')
  }
  if (normalizedOptions.rootDir !== undefined && normalizedOptions.rootDir !== null) {
    if (typeof normalizedOptions.rootDir !== 'string') {
      throw new TypeError('rootDir must be a string')
    }
    assertInsideRoot(archivePath, normalizedOptions.rootDir)
  }
  if (normalizedOptions.maxBytes !== undefined && normalizedOptions.maxBytes !== null) {
    if (!Number.isSafeInteger(normalizedOptions.maxBytes) || normalizedOptions.maxBytes < 0) {
      throw new RangeError('maxBytes must be a non-negative safe integer')
    }
    if (data.length > normalizedOptions.maxBytes) {
      const error = new RangeError('data exceeds maxBytes limit')
      error.code = 'ERR_NODE_STORMLIB_LIMIT'
      throw error
    }
  }

  return native.writeFile(archivePath, archivedName, data, normalizedOptions)
}

function compactArchive(archivePath, options) {
  const normalizedOptions = assertOptions(options)

  if (normalizedOptions.rootDir !== undefined && normalizedOptions.rootDir !== null) {
    if (typeof normalizedOptions.rootDir !== 'string') {
      throw new TypeError('rootDir must be a string')
    }
    assertInsideRoot(archivePath, normalizedOptions.rootDir)
  }

  return native.compactArchive(archivePath)
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
  createArchive,
  addFile,
  writeFile,
  compactArchive,
  compression,
}
