/// <reference types="node" />

import { Readable } from 'stream'

export interface ArchiveInfo {
  path: string
  fileCount?: number
  maxFileCount?: number
  sectorSize?: number
  archiveSize?: number
}

export interface ArchiveEntry {
  name: string
  plainName: string
  size: number
  compressedSize: number
  flags: number
  locale: number
  hashIndex: number
  blockIndex: number
}

export interface FileInfo {
  name: string
  size: number
  compressedSize?: number
  flags?: number
  locale?: number
  hashIndex?: number
  blockIndex?: number
  crc32?: number
}

export interface ListFilesOptions {
  maxEntries?: number
}

export interface ReadFileOptions {
  maxBytes?: number
  chunkSize?: number
}

export interface ExtractFileOptions {
  rootDir?: string
}

export type Compression =
  | 'none'
  | 'zlib'
  | 'pkware'
  | 'bzip2'
  | 'sparse'
  | 'lzma'
  | 'implode'

export interface CreateArchiveOptions {
  rootDir?: string
  overwrite?: boolean
  maxFileCount?: number
  version?: 1 | 2 | 3 | 4
}

export interface AddFileOptions {
  rootDir?: string
  sourceRootDir?: string
  maxBytes?: number
  compression?: Compression | number | boolean
  replaceExisting?: boolean
}

export interface WriteFileOptions {
  rootDir?: string
  maxBytes?: number
  compression?: Compression | number | boolean
  replaceExisting?: boolean
}

export interface CompactArchiveOptions {
  rootDir?: string
}

export const compression: Readonly<{
  none: 'none'
  zlib: 'zlib'
  pkware: 'pkware'
  bzip2: 'bzip2'
  sparse: 'sparse'
  lzma: 'lzma'
  implode: 'implode'
}>

export function getArchiveInfo(archivePath: string): ArchiveInfo
export function listFiles(
  archivePath: string,
  mask?: string,
  options?: ListFilesOptions,
): ArchiveEntry[]
export function listFiles(
  archivePath: string,
  options?: ListFilesOptions,
): ArchiveEntry[]
export function hasFile(archivePath: string, fileName: string): boolean
export function getFileInfo(archivePath: string, fileName: string): FileInfo
export function readFile(
  archivePath: string,
  fileName: string,
  options?: ReadFileOptions,
): Buffer
export function readFileAsync(
  archivePath: string,
  fileName: string,
  options?: ReadFileOptions,
): Promise<Buffer>
export function createReadStream(
  archivePath: string,
  fileName: string,
  options?: ReadFileOptions,
): Readable
export function extractFile(
  archivePath: string,
  fileName: string,
  outputPath: string,
  options?: ExtractFileOptions,
): boolean
export function createArchive(
  archivePath: string,
  options?: CreateArchiveOptions,
): boolean
export function addFile(
  archivePath: string,
  sourcePath: string,
  archivedName: string,
  options?: AddFileOptions,
): boolean
export function writeFile(
  archivePath: string,
  archivedName: string,
  data: Buffer,
  options?: WriteFileOptions,
): boolean
export function compactArchive(
  archivePath: string,
  options?: CompactArchiveOptions,
): boolean
