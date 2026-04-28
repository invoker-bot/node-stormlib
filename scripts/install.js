'use strict'

const fs = require('fs')
const path = require('path')
const { spawnSync } = require('child_process')

const rootDir = path.resolve(__dirname, '..')
const platformArch = `${process.platform}-${process.arch}`
const prebuiltPath = path.join(rootDir, 'prebuilds', platformArch, 'NodeStorm.node')
const buildPath = path.join(rootDir, 'build', 'Release', 'NodeStorm.node')
const forceBuild = process.env.NODE_STORMLIB_BUILD_FROM_SOURCE === '1' ||
  process.env.npm_config_build_from_source === 'true'

if (!forceBuild && fs.existsSync(prebuiltPath)) {
  process.stdout.write(`node-stormlib: using prebuilt addon ${platformArch}\n`)
  process.exit(0)
}

if (!forceBuild && fs.existsSync(buildPath)) {
  process.stdout.write('node-stormlib: using existing local native build\n')
  process.exit(0)
}

const npmCli = process.env.npm_execpath
const command = npmCli ? process.execPath : (process.platform === 'win32' ? 'npm.cmd' : 'npm')
const args = npmCli ? [npmCli, 'run', 'build'] : ['run', 'build']
const result = spawnSync(command, args, {
  cwd: rootDir,
  env: process.env,
  stdio: 'inherit',
})

if (result.error) {
  process.stderr.write(`node-stormlib: failed to start native build: ${result.error.message}\n`)
  process.exit(1)
}

process.exit(result.status === null ? 1 : result.status)
