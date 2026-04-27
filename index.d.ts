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

export interface ListFilesOptions {
  maxEntries?: number
}

export interface ReadFileOptions {
  maxBytes?: number
}

export interface ExtractFileOptions {
  rootDir?: string
}

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
