# node-storm

<p align="center">
  <strong>Node.js bindings for Blizzard MPQ archives, powered by StormLib.</strong>
</p>

<p align="center">
  <a href="https://github.com/invokerrrr/node-storm">
    <img alt="status" src="https://img.shields.io/badge/status-alpha-orange">
  </a>
  <img alt="license" src="https://img.shields.io/badge/license-MIT-blue">
  <img alt="node" src="https://img.shields.io/badge/node-%3E%3D18-brightgreen">
  <img alt="native" src="https://img.shields.io/badge/native-Node--API-333333">
</p>

`node-storm` exposes a small synchronous Node.js API for reading Blizzard MPQ
archives, including Warcraft III `.w3m` and `.w3x` maps. The native addon links
against the vendored [StormLib](https://github.com/ladislav-zezula/StormLib)
source tree.

## Features

| Capability | API |
| --- | --- |
| Read archive metadata | `getArchiveInfo()` |
| List files with StormLib masks | `listFiles()` |
| Check for a file | `hasFile()` |
| Read archive files as `Buffer` objects | `readFile()` |
| Extract archive files to disk | `extractFile()` |

## Requirements

- Node.js 18 or newer
- npm
- CMake
- A C++ compiler supported by CMake.js, such as Visual Studio Build Tools on Windows

## Installation

```sh
npm install
```

The install script builds `build/Release/NodeStorm.node` through CMake.js.

## Quick Start

```js
const storm = require('node-storm')

const archivePath = 'test/fixtures/maps/SaveAndLoad.w3x'

const info = storm.getArchiveInfo(archivePath)
const files = storm.listFiles(archivePath, 'war3map.*')
const mapInfo = storm.readFile(archivePath, 'war3map.w3i')

console.log(info.fileCount)
console.log(files.map(file => file.name))
console.log(mapInfo.length)
```

## API Reference

### `getArchiveInfo(archivePath)`

Opens an MPQ archive and returns basic metadata.

```js
{
  path: 'map.w3x',
  fileCount: 15,
  maxFileCount: 64,
  sectorSize: 4096,
  archiveSize: 15432
}
```

### `listFiles(archivePath, mask = '*')`

Returns matching archive entries. The optional `mask` uses StormLib wildcard
matching, for example `*.w3i` or `war3map.*`.

```js
const files = storm.listFiles('map.w3x', '*.w3i')
```

Each entry includes:

| Field | Description |
| --- | --- |
| `name` | Full archive path |
| `plainName` | Basename reported by StormLib |
| `size` | Uncompressed byte size |
| `compressedSize` | Stored byte size |
| `flags` | MPQ file flags |
| `locale` | File locale |
| `hashIndex` | Hash table index |
| `blockIndex` | Block table index |

### `hasFile(archivePath, fileName)`

Returns `true` when `fileName` exists in the archive.

```js
storm.hasFile('map.w3x', 'war3map.w3i')
```

### `readFile(archivePath, fileName)`

Reads a file from the archive and returns a Node.js `Buffer`.

```js
const bytes = storm.readFile('map.w3x', 'war3map.w3i')
```

### `extractFile(archivePath, fileName, outputPath)`

Extracts a file from the archive to `outputPath` and returns `true` on success.

```js
storm.extractFile('map.w3x', 'war3map.w3i', 'out/war3map.w3i')
```

## Development

```sh
npm run build
npm test
```

- `npm run build` compiles the native addon and StormLib static library.
- `npm test` runs Node.js unit tests against real `.w3x` and `.w3m` fixtures.

## Repository Layout

```text
.
|-- CMakeLists.txt              Native addon build entry
|-- index.js                    JavaScript package entry
|-- main.cc                     Node-API wrapper around StormLib
|-- test/fixtures/maps          MPQ map fixtures tracked by Git LFS
|-- test/open-archive.test.js   Unit tests for the public API
`-- third_party/StormLib        Vendored StormLib source
```

## Third-Party Code

StormLib is vendored from `ladislav-zezula/StormLib` at commit
`448dd9a824f60d940a4627f5d2b8a7c31ee9287f`. See
`third_party/README.md` and `third_party/StormLib/LICENSE` for details.

## Fixtures

Small Warcraft III map fixtures live in `test/fixtures/maps`. Binary archive
formats such as `.mpq`, `.w3m`, `.w3x`, and `.w3n` are tracked with Git LFS.

## License

This package is released under the MIT license.
