'use strict'

const assert = require('assert')
const fs = require('fs')
const os = require('os')
const path = require('path')
const test = require('node:test')

const storm = require('..')

const mapsDir = path.join(__dirname, 'fixtures', 'maps')
const saveAndLoadMap = path.join(mapsDir, 'SaveAndLoad.w3x')
const musicMap = path.join(mapsDir, 'Music.w3x')
const rocMap = path.join(mapsDir, 'FunctionsWhichStopUnits.w3m')

async function collectStream(readable) {
  const chunks = []
  for await (const chunk of readable) {
    chunks.push(chunk)
  }

  return Buffer.concat(chunks)
}

test('exports the MPQ archive API', () => {
  assert.strictEqual(typeof storm.getArchiveInfo, 'function')
  assert.strictEqual(typeof storm.listFiles, 'function')
  assert.strictEqual(typeof storm.hasFile, 'function')
  assert.strictEqual(typeof storm.getFileInfo, 'function')
  assert.strictEqual(typeof storm.readFile, 'function')
  assert.strictEqual(typeof storm.readFileAsync, 'function')
  assert.strictEqual(typeof storm.createReadStream, 'function')
  assert.strictEqual(typeof storm.extractFile, 'function')
  assert.strictEqual(typeof storm.createArchive, 'function')
  assert.strictEqual(typeof storm.addFile, 'function')
  assert.strictEqual(typeof storm.writeFile, 'function')
  assert.strictEqual(typeof storm.compactArchive, 'function')
  assert.strictEqual(typeof storm.compression, 'object')
})

test('opens Warcraft III map archives and reads archive metadata', () => {
  for (const archivePath of [saveAndLoadMap, musicMap, rocMap]) {
    const info = storm.getArchiveInfo(archivePath)

    assert.strictEqual(info.path, archivePath)
    assert.ok(info.archiveSize > 0)
    assert.ok(info.fileCount > 0)
    assert.ok(info.maxFileCount >= info.fileCount)
    assert.ok(info.sectorSize > 0)
  }
})

test('lists archive entries and supports masks', () => {
  const files = storm.listFiles(saveAndLoadMap)
  const names = files.map(file => file.name)

  assert.ok(files.length > 0)
  assert.ok(names.includes('war3map.w3i'))
  assert.ok(names.includes('war3map.wts'))

  const infoFiles = storm.listFiles(saveAndLoadMap, '*.w3i')
  assert.deepStrictEqual(infoFiles.map(file => file.name), ['war3map.w3i'])
  assert.strictEqual(infoFiles[0].plainName, 'war3map.w3i')
  assert.ok(infoFiles[0].size > 0)
  assert.ok(infoFiles[0].compressedSize > 0)
})

test('checks file existence inside an archive', () => {
  assert.strictEqual(storm.hasFile(saveAndLoadMap, 'war3map.w3i'), true)
  assert.strictEqual(storm.hasFile(saveAndLoadMap, 'missing.txt'), false)
})

test('reads metadata for one file inside an archive', () => {
  const contents = storm.readFile(saveAndLoadMap, 'war3map.w3i')
  const info = storm.getFileInfo(saveAndLoadMap, 'war3map.w3i')

  assert.strictEqual(info.name, 'war3map.w3i')
  assert.strictEqual(info.size, contents.length)
  assert.ok(info.compressedSize > 0)
  assert.strictEqual(typeof info.flags, 'number')
  assert.strictEqual(typeof info.locale, 'number')
  assert.strictEqual(typeof info.blockIndex, 'number')
})

test('reads files from an archive as buffers', () => {
  const contents = storm.readFile(saveAndLoadMap, 'war3map.w3i')

  assert.ok(Buffer.isBuffer(contents))
  assert.ok(contents.length > 0)
})

test('reads files asynchronously and as readable streams', async () => {
  const expected = storm.readFile(saveAndLoadMap, 'war3map.w3i')
  const asyncContents = await storm.readFileAsync(saveAndLoadMap, 'war3map.w3i')
  const streamContents = await collectStream(
    storm.createReadStream(saveAndLoadMap, 'war3map.w3i', { chunkSize: 16 }),
  )

  assert.deepStrictEqual(asyncContents, expected)
  assert.deepStrictEqual(streamContents, expected)
})

test('readable streams enforce maxBytes before reading chunks', async () => {
  await assert.rejects(
    collectStream(storm.createReadStream(saveAndLoadMap, 'war3map.w3i', { maxBytes: 1 })),
    /maxBytes|exceeds/i,
  )
})

test('preserves native error metadata for asynchronous reads', async () => {
  await assert.rejects(
    storm.readFileAsync(saveAndLoadMap, 'missing.txt'),
    error => {
      assert.match(error.code, /^STORM_/)
      assert.strictEqual(typeof error.stormCode, 'number')
      return true
    },
  )
})

test('extracts files from an archive', () => {
  const contents = storm.readFile(saveAndLoadMap, 'war3map.w3i')
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'node-stormlib-'))

  try {
    const outputPath = path.join(tempDir, 'war3map.w3i')
    assert.strictEqual(
      storm.extractFile(saveAndLoadMap, 'war3map.w3i', outputPath),
      true,
    )
    assert.deepStrictEqual(fs.readFileSync(outputPath), contents)
  } finally {
    fs.rmSync(tempDir, { recursive: true, force: true })
  }
})

test('throws useful errors for missing archives and files', () => {
  assert.throws(
    () => storm.getArchiveInfo(path.join(mapsDir, 'missing.w3x')),
    /SFileOpenArchive failed with StormLib error/,
  )
  assert.throws(
    () => storm.readFile(saveAndLoadMap, 'missing.txt'),
    /SFileOpenFileEx failed with StormLib error/,
  )
})

test('validates argument types', () => {
  assert.throws(() => storm.getArchiveInfo(), /archivePath must be a string/)
  assert.throws(() => storm.listFiles(saveAndLoadMap, 1), /mask must be a string/)
  assert.throws(() => storm.hasFile(saveAndLoadMap), /fileName must be a string/)
  assert.throws(() => storm.extractFile(saveAndLoadMap, 'war3map.w3i'), /outputPath must be a string/)
})
