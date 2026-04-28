#!/usr/bin/env node
'use strict'

const fs = require('fs')
const path = require('path')
const storm = require('..')

const DEFAULT_MAX_BYTES = 512 * 1024 * 1024
const DEFAULT_MAX_ENTRIES = 10000

const valueOptions = new Set([
  'compression',
  'max-bytes',
  'max-entries',
  'max-files',
  'max-file-count',
  'mask',
  'prefix',
  'root',
  'source-root',
  'version',
])
const booleanOptions = new Set(['help', 'json', 'overwrite', 'no-limits', 'no-replace'])

function help() {
  return `Usage:
  mpq info <archive> [--json]
  mpq list <archive> [mask] [--json] [--max-entries <n>] [--no-limits]
  mpq extract <archive> <file> <output> [--root <dir>] [--max-bytes <n>] [--no-limits]
  mpq unpack <archive> <output-dir> [--mask <mask>] [--max-entries <n>] [--max-bytes <n>] [--no-limits]
  mpq create <archive> [--root <dir>] [--overwrite] [--max-files <n>] [--version <1-4>]
  mpq add <archive> <source> <name> [--root <dir>] [--source-root <dir>] [--compression <name>] [--max-bytes <n>] [--no-limits] [--no-replace]
  mpq pack <source-dir> <archive> [--root <dir>] [--overwrite] [--compression <name>] [--max-bytes <n>] [--prefix <path>] [--no-limits]
  mpq compact <archive> [--root <dir>]

Aliases:
  ls=list, x=extract, extract-all=unpack, decompress=unpack, compress=pack

Safety defaults:
  --max-bytes defaults to ${DEFAULT_MAX_BYTES} bytes for reads and writes.
  --max-entries defaults to ${DEFAULT_MAX_ENTRIES} for list and unpack.
  Use --no-limits to disable these defaults, or pass explicit limits to tighten them.

Compression:
  none, zlib, pkware, bzip2, sparse, lzma, implode
`
}

function fail(message) {
  process.stderr.write(`mpq: ${message}\n`)
  process.exit(1)
}

function normalizeOptionName(name) {
  return name.replace(/^-+/, '').toLowerCase()
}

function setOption(options, name, value) {
  switch (name) {
    case 'max-bytes':
      options.maxBytes = value
      break
    case 'max-entries':
      options.maxEntries = value
      break
    case 'max-file-count':
    case 'max-files':
      options.maxFileCount = value
      break
    case 'source-root':
      options.sourceRoot = value
      break
    case 'no-limits':
      options.noLimits = value
      break
    default:
      options[name] = value
      break
  }
}

function parseArgs(argv) {
  const positionals = []
  const options = {}

  for (let index = 0; index < argv.length; index++) {
    const arg = argv[index]

    if (arg === '--') {
      positionals.push(...argv.slice(index + 1))
      break
    }

    if (arg === '-h') {
      options.help = true
      continue
    }

    if (!arg.startsWith('--')) {
      positionals.push(arg)
      continue
    }

    const equalsIndex = arg.indexOf('=')
    const rawName = equalsIndex === -1 ? arg : arg.slice(0, equalsIndex)
    const name = normalizeOptionName(rawName)
    const inlineValue = equalsIndex === -1 ? undefined : arg.slice(equalsIndex + 1)

    if (name === 'no-replace') {
      options.replaceExisting = false
      continue
    }

    if (booleanOptions.has(name)) {
      setOption(options, name, inlineValue === undefined ? true : inlineValue !== 'false')
      continue
    }

    if (!valueOptions.has(name)) {
      fail(`unknown option --${name}`)
    }

    const value = inlineValue === undefined ? argv[++index] : inlineValue
    if (value === undefined) {
      fail(`--${name} requires a value`)
    }

    setOption(options, name, value)
  }

  return { positionals, options }
}

function parseNumber(value, name) {
  if (value === undefined || value === null) {
    return undefined
  }

  const number = Number(value)
  if (!Number.isSafeInteger(number) || number < 0) {
    fail(`${name} must be a non-negative integer`)
  }

  return number
}

function limitOption(options, fieldName, optionName, defaultValue) {
  const explicitValue = parseNumber(options[fieldName], optionName)
  if (explicitValue !== undefined) {
    return explicitValue
  }

  return options.noLimits ? undefined : defaultValue
}

function maxBytesLimit(options) {
  return limitOption(options, 'maxBytes', 'max-bytes', DEFAULT_MAX_BYTES)
}

function maxEntriesLimit(options) {
  return limitOption(options, 'maxEntries', 'max-entries', DEFAULT_MAX_ENTRIES)
}

function requireArgs(command, positionals, count) {
  if (positionals.length < count) {
    fail(`${command} expects ${count} argument${count === 1 ? '' : 's'}\n\n${help()}`)
  }
}

function archiveOptions(options) {
  return {
    rootDir: options.root,
    overwrite: options.overwrite,
    maxFileCount: parseNumber(options.maxFileCount, 'max-files'),
    version: parseNumber(options.version, 'version'),
  }
}

function addOptions(options, sourceRoot) {
  return {
    rootDir: options.root,
    sourceRootDir: options.sourceRoot || sourceRoot,
    maxBytes: maxBytesLimit(options),
    compression: options.compression,
    replaceExisting: options.replaceExisting,
  }
}

function readOptions(options) {
  return {
    maxBytes: maxBytesLimit(options),
  }
}

function rootOptions(options, rootDir) {
  return {
    rootDir: options.root || rootDir,
  }
}

function ensureInside(rootDir, outputPath) {
  const root = path.resolve(rootDir)
  const target = path.resolve(outputPath)
  const relative = path.relative(root, target)

  if (relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative))) {
    return target
  }

  fail(`refusing to write outside ${rootDir}: ${outputPath}`)
}

function archiveNameToPath(rootDir, archiveName) {
  const normalized = archiveName.replace(/\\/g, '/')
  if (path.posix.isAbsolute(normalized) || /^[A-Za-z]:/.test(normalized)) {
    fail(`unsafe archive entry name: ${archiveName}`)
  }

  const segments = normalized.split('/')
  if (segments.some(segment => segment === '' || segment === '.' || segment === '..')) {
    fail(`unsafe archive entry name: ${archiveName}`)
  }

  return ensureInside(rootDir, path.join(rootDir, ...segments))
}

function ensureArchiveFileWithinLimit(archivePath, fileName, maxBytes) {
  if (maxBytes === undefined) {
    return
  }

  const fileInfo = storm.getFileInfo(archivePath, fileName)
  if (fileInfo.size > maxBytes) {
    fail(`${fileName} exceeds max-bytes limit (${fileInfo.size} > ${maxBytes})`)
  }
}

function walkFiles(rootDir, skipPath, results = []) {
  for (const entry of fs.readdirSync(rootDir, { withFileTypes: true })) {
    const fullPath = path.join(rootDir, entry.name)
    if (skipPath && path.resolve(fullPath) === skipPath) {
      continue
    }

    if (entry.isDirectory()) {
      walkFiles(fullPath, skipPath, results)
    } else if (entry.isFile()) {
      results.push(fullPath)
    }
  }

  return results
}

function toArchiveName(sourceRoot, filePath, prefix) {
  const relative = path.relative(sourceRoot, filePath).split(path.sep).join('/')
  if (!prefix) {
    return relative
  }

  return `${prefix.replace(/\\/g, '/').replace(/\/+$/, '')}/${relative}`
}

function commandInfo(args, options) {
  requireArgs('info', args, 1)
  const info = storm.getArchiveInfo(args[0])
  process.stdout.write(options.json ? `${JSON.stringify(info, null, 2)}\n` : [
    `path: ${info.path}`,
    `files: ${info.fileCount}`,
    `maxFiles: ${info.maxFileCount}`,
    `sectorSize: ${info.sectorSize}`,
    `archiveSize: ${info.archiveSize}`,
  ].join('\n') + '\n')
}

function commandList(args, options) {
  requireArgs('list', args, 1)
  const mask = args[1] || options.mask || '*'
  const files = storm.listFiles(args[0], mask, {
    maxEntries: maxEntriesLimit(options),
  })

  if (options.json) {
    process.stdout.write(`${JSON.stringify(files, null, 2)}\n`)
    return
  }

  for (const file of files) {
    process.stdout.write(`${file.name}\n`)
  }
}

function commandExtract(args, options) {
  requireArgs('extract', args, 3)
  const [archivePath, fileName, outputPath] = args
  const rootDir = options.root ? path.resolve(options.root) : path.dirname(path.resolve(outputPath))
  const safeOutputPath = ensureInside(rootDir, outputPath)
  const maxBytes = maxBytesLimit(options)

  fs.mkdirSync(path.dirname(safeOutputPath), { recursive: true })
  ensureArchiveFileWithinLimit(archivePath, fileName, maxBytes)
  storm.extractFile(archivePath, fileName, safeOutputPath, rootOptions(options, rootDir))

  process.stdout.write(`Extracted ${fileName} to ${safeOutputPath}\n`)
}

function commandUnpack(args, options) {
  requireArgs('unpack', args, 2)
  const [archivePath, outputDir] = args
  const rootDir = path.resolve(outputDir)
  const mask = options.mask || '*'
  const files = storm.listFiles(archivePath, mask, {
    maxEntries: maxEntriesLimit(options),
  })
  const readLimits = readOptions(options)

  fs.mkdirSync(rootDir, { recursive: true })
  for (const file of files) {
    const outputPath = archiveNameToPath(rootDir, file.name)
    fs.mkdirSync(path.dirname(outputPath), { recursive: true })
    ensureArchiveFileWithinLimit(archivePath, file.name, readLimits.maxBytes)
    storm.extractFile(archivePath, file.name, outputPath, { rootDir })
  }

  process.stdout.write(`Extracted ${files.length} file${files.length === 1 ? '' : 's'} to ${rootDir}\n`)
}

function commandCreate(args, options) {
  requireArgs('create', args, 1)
  storm.createArchive(args[0], archiveOptions(options))
  process.stdout.write(`Created ${path.resolve(args[0])}\n`)
}

function commandAdd(args, options) {
  requireArgs('add', args, 3)
  storm.addFile(args[0], args[1], args[2], addOptions(options))
  process.stdout.write(`Added ${args[1]} as ${args[2]}\n`)
}

function commandPack(args, options) {
  requireArgs('pack', args, 2)
  const sourceRoot = path.resolve(args[0])
  const archivePath = path.resolve(args[1])
  const sourceStats = fs.statSync(sourceRoot)
  if (!sourceStats.isDirectory()) {
    fail('source-dir must be a directory')
  }

  const files = walkFiles(sourceRoot, archivePath)
  const createOptions = archiveOptions(options)
  createOptions.maxFileCount = createOptions.maxFileCount || Math.max(files.length + 4, 4)
  storm.createArchive(archivePath, createOptions)

  for (const filePath of files) {
    storm.addFile(
      archivePath,
      filePath,
      toArchiveName(sourceRoot, filePath, options.prefix),
      addOptions(options, sourceRoot),
    )
  }

  process.stdout.write(`Packed ${files.length} file${files.length === 1 ? '' : 's'} into ${archivePath}\n`)
}

function commandCompact(args, options) {
  requireArgs('compact', args, 1)
  storm.compactArchive(args[0], rootOptions(options))
  process.stdout.write(`Compacted ${path.resolve(args[0])}\n`)
}

function main() {
  const { positionals, options } = parseArgs(process.argv.slice(2))
  let command = positionals.shift()

  if (!command || options.help) {
    process.stdout.write(help())
    return
  }

  const aliases = {
    compress: 'pack',
    decompress: 'unpack',
    'extract-all': 'unpack',
    ls: 'list',
    x: 'extract',
  }
  command = aliases[command] || command

  const commands = {
    add: commandAdd,
    compact: commandCompact,
    create: commandCreate,
    extract: commandExtract,
    info: commandInfo,
    list: commandList,
    pack: commandPack,
    unpack: commandUnpack,
  }

  if (!commands[command]) {
    fail(`unknown command ${command}\n\n${help()}`)
  }

  commands[command](positionals, options)
}

try {
  main()
} catch (error) {
  const details = error.code ? ` (${error.code})` : ''
  fail(`${error.message}${details}`)
}
