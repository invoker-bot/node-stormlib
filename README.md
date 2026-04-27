# node-storm

Node.js bindings for [StormLib](https://github.com/ladislav-zezula/StormLib), a native library for reading Blizzard MPQ archives such as Warcraft III `.w3m` and `.w3x` maps.

## Requirements

- Node.js 18 or newer
- npm
- CMake
- A C++ compiler supported by CMake.js, such as Visual Studio Build Tools on Windows

## Installation

```sh
npm install
```

The install script builds the native addon with CMake.js and links the vendored StormLib source from `third_party/StormLib`.

## Development Commands

```sh
npm run build
npm test
```

- `npm run build` compiles `build/Release/NodeStorm.node`.
- `npm test` runs the Node.js unit tests against MPQ map fixtures in `test/fixtures/maps`.

## Usage

```js
const storm = require('node-storm')

const archivePath = 'test/fixtures/maps/SaveAndLoad.w3x'

const info = storm.getArchiveInfo(archivePath)
const files = storm.listFiles(archivePath)
const script = storm.readFile(archivePath, 'war3map.w3i')

console.log(info.fileCount)
console.log(files.map(file => file.name))
console.log(script.length)
```

## API

### `getArchiveInfo(archivePath)`

Opens an MPQ archive and returns metadata:

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

Returns matching archive entries. The `mask` argument uses StormLib wildcard matching, for example `*.w3i` or `war3map.*`.

Each entry contains `name`, `plainName`, `size`, `compressedSize`, `flags`, `locale`, `hashIndex`, and `blockIndex`.

### `hasFile(archivePath, fileName)`

Returns `true` when `fileName` exists in the archive, otherwise `false`.

### `readFile(archivePath, fileName)`

Reads a file from the archive and returns a Node.js `Buffer`.

### `extractFile(archivePath, fileName, outputPath)`

Extracts a file from the archive to `outputPath` and returns `true` on success.

## Test Fixtures

Small `.w3x` and `.w3m` fixtures live in `test/fixtures/maps`. Binary map formats are tracked by Git LFS through `.gitattributes`.
