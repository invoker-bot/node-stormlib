'use strict'

const assert = require('assert')
const { spawnSync } = require('child_process')
const fs = require('fs')
const os = require('os')
const path = require('path')
const test = require('node:test')

const cliPath = path.join(__dirname, '..', 'bin', 'mpq.js')

function runMpq(args, options = {}) {
  return spawnSync(process.execPath, [cliPath, ...args], {
    cwd: path.join(__dirname, '..'),
    encoding: 'utf8',
    ...options,
  })
}

function withTempDir(callback) {
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'node-storm-cli-'))

  try {
    return callback(tempDir)
  } finally {
    fs.rmSync(tempDir, { recursive: true, force: true })
  }
}

test('prints CLI help', () => {
  const result = runMpq(['--help'])

  assert.strictEqual(result.status, 0)
  assert.match(result.stdout, /Usage:/)
  assert.match(result.stdout, /mpq pack/)
  assert.match(result.stdout, /mpq unpack/)
})

test('packs a directory, lists entries, extracts one file, and unpacks safely', () => {
  withTempDir(tempDir => {
    const sourceDir = path.join(tempDir, 'source')
    const nestedDir = path.join(sourceDir, 'nested')
    const archivePath = path.join(tempDir, 'packed.mpq')
    const extractPath = path.join(tempDir, 'single', 'alpha.txt')
    const unpackDir = path.join(tempDir, 'unpacked')

    fs.mkdirSync(nestedDir, { recursive: true })
    fs.writeFileSync(path.join(sourceDir, 'alpha.txt'), 'alpha')
    fs.writeFileSync(path.join(nestedDir, 'beta.txt'), 'beta')

    const pack = runMpq([
      'pack',
      sourceDir,
      archivePath,
      '--root',
      tempDir,
      '--overwrite',
      '--compression',
      'zlib',
    ])
    assert.strictEqual(pack.status, 0, pack.stderr)
    assert.match(pack.stdout, /Packed 2 files/)

    const list = runMpq(['list', archivePath, '--json'])
    assert.strictEqual(list.status, 0, list.stderr)
    const entries = JSON.parse(list.stdout)
    const names = entries.map(entry => entry.name).sort()
    assert.ok(names.includes('alpha.txt'))
    assert.ok(names.includes('nested/beta.txt'))

    const extract = runMpq([
      'extract',
      archivePath,
      'alpha.txt',
      extractPath,
      '--root',
      path.join(tempDir, 'single'),
      '--max-bytes',
      '1024',
    ])
    assert.strictEqual(extract.status, 0, extract.stderr)
    assert.strictEqual(fs.readFileSync(extractPath, 'utf8'), 'alpha')

    const unpack = runMpq([
      'unpack',
      archivePath,
      unpackDir,
      '--max-bytes',
      '1024',
    ])
    assert.strictEqual(unpack.status, 0, unpack.stderr)
    assert.strictEqual(fs.readFileSync(path.join(unpackDir, 'alpha.txt'), 'utf8'), 'alpha')
    assert.strictEqual(fs.readFileSync(path.join(unpackDir, 'nested', 'beta.txt'), 'utf8'), 'beta')
  })
})

test('adds a file through the CLI and rejects unsafe archive names', () => {
  withTempDir(tempDir => {
    const archivePath = path.join(tempDir, 'manual.mpq')
    const sourcePath = path.join(tempDir, 'payload.txt')

    fs.writeFileSync(sourcePath, 'payload')

    const create = runMpq(['create', archivePath, '--root', tempDir, '--max-files', '8'])
    assert.strictEqual(create.status, 0, create.stderr)

    const add = runMpq([
      'add',
      archivePath,
      sourcePath,
      'docs/payload.txt',
      '--root',
      tempDir,
      '--source-root',
      tempDir,
      '--compression',
      'pkware',
    ])
    assert.strictEqual(add.status, 0, add.stderr)

    const unsafe = runMpq([
      'add',
      archivePath,
      sourcePath,
      '../payload.txt',
      '--root',
      tempDir,
      '--source-root',
      tempDir,
    ])
    assert.notStrictEqual(unsafe.status, 0)
    assert.match(unsafe.stderr, /archivedName|unsafe|traversal|relative/i)

    const extract = runMpq([
      'extract',
      archivePath,
      'docs/payload.txt',
      path.join(tempDir, 'out', 'payload.txt'),
      '--root',
      path.join(tempDir, 'out'),
    ])
    assert.strictEqual(extract.status, 0, extract.stderr)
    assert.strictEqual(fs.readFileSync(path.join(tempDir, 'out', 'payload.txt'), 'utf8'), 'payload')
  })
})
