'use strict'

const assert = require('assert')
const { execSync } = require('child_process')
const fs = require('fs')
const os = require('os')
const path = require('path')
const test = require('node:test')

const packageJson = require('../package.json')
const storm = require('..')

const repoRoot = path.resolve(__dirname, '..')
const mapsDir = path.join(__dirname, 'fixtures', 'maps')
const archivePath = path.join(mapsDir, 'SaveAndLoad.w3x')
const archiveFileName = 'war3map.w3i'

function packedFilePaths() {
  const output = execSync('npm pack --dry-run --json', {
    cwd: repoRoot,
    encoding: 'utf8',
  })

  return JSON.parse(output)[0].files.map(file => file.path)
}

test('published package excludes test fixtures and MPQ map binaries', () => {
  const publishedFiles = packedFilePaths()
  const forbiddenFiles = publishedFiles.filter(filePath => {
    return filePath.startsWith('test/') || /\.(mpq|w3m|w3x|w3n)$/i.test(filePath)
  })

  assert.deepStrictEqual(forbiddenFiles, [])
})

test('published package has build tools available during production install', () => {
  const installScript = packageJson.scripts && packageJson.scripts.install
  const buildScript = packageJson.scripts && packageJson.scripts.build
  const installsByBuilding = /npm run build|cmake-js/.test(installScript || '')
  const buildUsesCmakeJs = /cmake-js/.test(buildScript || '')

  assert.ok(
    !installsByBuilding || !buildUsesCmakeJs || packageJson.dependencies['cmake-js'],
    'install builds with cmake-js, but cmake-js is only listed in devDependencies',
  )
})

test('readFile enforces a caller-provided maximum byte limit', () => {
  assert.throws(
    () => storm.readFile(archivePath, archiveFileName, { maxBytes: 1 }),
    /maxBytes|too large|exceeds/i,
  )
})

test('listFiles enforces a caller-provided maximum entry limit', () => {
  const files = storm.listFiles(archivePath, '*', { maxEntries: 1 })

  assert.ok(files.length <= 1, `expected at most 1 file, got ${files.length}`)
})

test('extractFile refuses to write outside an allowed output directory', () => {
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'node-storm-review-'))
  const allowedRoot = path.join(tempDir, 'allowed')
  const escapedOutputPath = path.join(tempDir, 'escaped.w3i')

  fs.mkdirSync(allowedRoot)

  try {
    assert.throws(
      () => storm.extractFile(
        archivePath,
        archiveFileName,
        escapedOutputPath,
        { rootDir: allowedRoot },
      ),
      /outside|root|directory|traversal/i,
    )
    assert.strictEqual(fs.existsSync(escapedOutputPath), false)
  } finally {
    fs.rmSync(tempDir, { recursive: true, force: true })
  }
})

test('Windows builds open archive paths containing non-ASCII characters', {
  skip: process.platform !== 'win32',
}, () => {
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), '节点-storm-'))
  const unicodeArchivePath = path.join(tempDir, '地图.w3x')

  fs.copyFileSync(archivePath, unicodeArchivePath)

  try {
    assert.doesNotThrow(() => storm.getArchiveInfo(unicodeArchivePath))
  } finally {
    fs.rmSync(tempDir, { recursive: true, force: true })
  }
})

test('StormLib errors expose a stable JavaScript error code', () => {
  assert.throws(
    () => storm.readFile(archivePath, 'missing.txt'),
    error => {
      assert.ok(error.code, 'expected native errors to include error.code')
      assert.match(error.code, /^STORM_|^ENOENT$/)
      return true
    },
  )
})

test('CI validates native builds on Windows, Linux, and macOS', () => {
  const workflow = fs.readFileSync(
    path.join(repoRoot, '.github', 'workflows', 'ci-publish.yml'),
    'utf8',
  )

  assert.match(workflow, /strategy:/)
  assert.match(workflow, /windows-latest/)
  assert.match(workflow, /ubuntu-latest/)
  assert.match(workflow, /macos-latest/)
})

test('package exposes TypeScript declarations', () => {
  assert.ok(packageJson.types || packageJson.typings, 'expected a types or typings field')
})

test('package exposes non-blocking archive read APIs', () => {
  assert.strictEqual(typeof storm.readFileAsync, 'function')
  assert.strictEqual(typeof storm.createReadStream, 'function')
})

test('package has a prebuilt binary distribution path', () => {
  const allDependencies = {
    ...packageJson.dependencies,
    ...packageJson.devDependencies,
  }
  const allScripts = Object.values(packageJson.scripts || {}).join(' ')
  const hasPrebuildTooling = /prebuild|prebuildify|node-pre-gyp/.test(allScripts) ||
    Boolean(allDependencies['prebuild-install']) ||
    Boolean(allDependencies['@mapbox/node-pre-gyp'])

  assert.ok(packageJson.binary || hasPrebuildTooling, 'expected prebuilt binary tooling')
})
