'use strict'

const assert = require('assert')
const fs = require('fs')
const os = require('os')
const path = require('path')
const test = require('node:test')

const storm = require('..')

const MPQ_FILE_COMPRESS_MASK = 0x0000ff00

function withTempDir(callback) {
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'node-storm-write-'))

  try {
    return callback(tempDir)
  } finally {
    fs.rmSync(tempDir, { recursive: true, force: true })
  }
}

test('creates MPQ archives and writes compressed files', () => {
  withTempDir(tempDir => {
    const archivePath = path.join(tempDir, 'created.mpq')
    const sourceRoot = path.join(tempDir, 'source')
    const sourcePath = path.join(sourceRoot, 'payload.txt')
    const payload = Buffer.from('node-storm compressed payload\n'.repeat(512))

    fs.mkdirSync(sourceRoot)
    fs.writeFileSync(sourcePath, payload)

    assert.strictEqual(
      storm.createArchive(archivePath, {
        rootDir: tempDir,
        maxFileCount: 16,
        version: 1,
      }),
      true,
    )
    assert.strictEqual(
      storm.addFile(archivePath, sourcePath, 'docs/payload.txt', {
        rootDir: tempDir,
        sourceRootDir: sourceRoot,
        maxBytes: payload.length,
        compression: storm.compression.zlib,
      }),
      true,
    )

    const entries = storm.listFiles(archivePath, 'docs/*')
    assert.deepStrictEqual(entries.map(entry => entry.name), ['docs/payload.txt'])
    assert.notStrictEqual(entries[0].flags & MPQ_FILE_COMPRESS_MASK, 0)
    assert.deepStrictEqual(storm.readFile(archivePath, 'docs/payload.txt'), payload)
  })
})

test('writes compressed Buffer contents directly into MPQ archives', () => {
  withTempDir(tempDir => {
    const archivePath = path.join(tempDir, 'buffer.mpq')
    const payload = Buffer.from('buffer payload\n'.repeat(256))

    storm.createArchive(archivePath, { rootDir: tempDir, maxFileCount: 8 })
    assert.strictEqual(
      storm.writeFile(archivePath, 'buffer/payload.txt', payload, {
        rootDir: tempDir,
        maxBytes: payload.length,
        compression: storm.compression.zlib,
      }),
      true,
    )

    const entries = storm.listFiles(archivePath, 'buffer/*')
    assert.deepStrictEqual(entries.map(entry => entry.name), ['buffer/payload.txt'])
    assert.notStrictEqual(entries[0].flags & MPQ_FILE_COMPRESS_MASK, 0)
    assert.deepStrictEqual(storm.readFile(archivePath, 'buffer/payload.txt'), payload)
  })
})

test('overwrites existing output only when explicitly allowed', () => {
  withTempDir(tempDir => {
    const archivePath = path.join(tempDir, 'overwrite.mpq')

    fs.writeFileSync(archivePath, 'existing')

    assert.throws(
      () => storm.createArchive(archivePath, { rootDir: tempDir }),
      error => error.code === 'EEXIST',
    )
    assert.strictEqual(
      storm.createArchive(archivePath, { rootDir: tempDir, overwrite: true }),
      true,
    )
    assert.ok(storm.getArchiveInfo(archivePath).archiveSize > 0)
  })
})

test('write APIs enforce root and source safety limits', () => {
  withTempDir(tempDir => {
    const archivePath = path.join(tempDir, 'safe.mpq')
    const allowedRoot = path.join(tempDir, 'allowed')
    const sourceRoot = path.join(tempDir, 'source')
    const sourcePath = path.join(sourceRoot, 'payload.txt')
    const outsideSourcePath = path.join(tempDir, 'outside.txt')

    fs.mkdirSync(allowedRoot)
    fs.mkdirSync(sourceRoot)
    fs.writeFileSync(sourcePath, 'inside')
    fs.writeFileSync(outsideSourcePath, 'outside')

    storm.createArchive(archivePath, { rootDir: tempDir })

    assert.throws(
      () => storm.createArchive(archivePath, { rootDir: allowedRoot, overwrite: true }),
      /outside|root|directory|traversal/i,
    )
    assert.throws(
      () => storm.addFile(archivePath, outsideSourcePath, 'outside.txt', {
        rootDir: tempDir,
        sourceRootDir: sourceRoot,
      }),
      /outside|root|directory|traversal/i,
    )
    assert.throws(
      () => storm.addFile(archivePath, sourcePath, '../payload.txt', {
        rootDir: tempDir,
        sourceRootDir: sourceRoot,
      }),
      /archivedName|traversal|relative/i,
    )
    assert.throws(
      () => storm.addFile(archivePath, sourcePath, 'payload.txt', {
        rootDir: tempDir,
        sourceRootDir: sourceRoot,
        maxBytes: 1,
      }),
      /maxBytes|too large|exceeds/i,
    )
    assert.throws(
      () => storm.writeFile(archivePath, '/payload.txt', Buffer.from('payload'), {
        rootDir: tempDir,
      }),
      /archivedName|traversal|relative/i,
    )
    assert.throws(
      () => storm.writeFile(archivePath, 'C:\\payload.txt', Buffer.from('payload'), {
        rootDir: tempDir,
      }),
      /archivedName|traversal|relative/i,
    )
    assert.throws(
      () => storm.writeFile(archivePath, 'payload.txt', Buffer.from('payload'), {
        rootDir: tempDir,
        maxBytes: 1,
      }),
      /maxBytes|too large|exceeds/i,
    )
  })
})

test('compacts writable MPQ archives', () => {
  withTempDir(tempDir => {
    const archivePath = path.join(tempDir, 'compact.mpq')
    const sourcePath = path.join(tempDir, 'payload.txt')

    fs.writeFileSync(sourcePath, 'compact me')
    storm.createArchive(archivePath, { rootDir: tempDir, maxFileCount: 8 })
    storm.addFile(archivePath, sourcePath, 'payload.txt', {
      rootDir: tempDir,
      sourceRootDir: tempDir,
      compression: storm.compression.pkware,
    })

    assert.strictEqual(storm.compactArchive(archivePath, { rootDir: tempDir }), true)
    assert.strictEqual(storm.readFile(archivePath, 'payload.txt').toString(), 'compact me')
  })
})
