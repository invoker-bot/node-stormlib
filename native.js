'use strict'

const path = require('path')

function canFallbackFromPrebuiltError(error, prebuiltPath) {
  if (error.code !== 'MODULE_NOT_FOUND') {
    return true
  }

  return error.message.includes(prebuiltPath)
}

function loadNativeAddon() {
  const prebuiltPath = path.join(
    __dirname,
    'prebuilds',
    `${process.platform}-${process.arch}`,
    'NodeStorm.node',
  )

  try {
    return require(prebuiltPath)
  } catch (error) {
    if (canFallbackFromPrebuiltError(error, prebuiltPath)) {
      return require('bindings')('NodeStorm.node')
    }

    throw error
  }
}

module.exports = loadNativeAddon()
