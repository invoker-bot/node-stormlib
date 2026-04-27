# Repository Guidelines

## Project Structure & Module Organization

This repository is a small Node.js native addon for working with Blizzard MPQ archive formats.

- `index.js` is the package entry point and loads the compiled native addon through `bindings`.
- `main.cc` contains the Node-API addon implementation.
- `CMakeLists.txt` defines the native build target, currently `NodeStorm`.
- `cmake/` contains helper modules for CMake.js and `node-addon-api` integration.
- `package.json` and `package-lock.json` define the Node package metadata and runtime dependencies.

Keep new C++ sources near `main.cc` until the addon grows enough to justify a `src/` directory. Add generated build output only under ignored paths such as `build/`, `out/`, or `dist/`.

## Build, Test, and Development Commands

- `npm install`: installs dependencies and runs the package `install` script, which compiles the native addon with `cmake-js compile`.
- `npm run install`: manually reruns the native compile step.
- `npx cmake-js compile`: directly builds the addon when CMake.js is available through npm or globally.
- `npm test`: currently fails with the placeholder `"Error: no test specified"` message; update this script when adding tests.

Native builds require Node.js, npm, CMake, a C++ compiler, and CMake.js.

## Coding Style & Naming Conventions

Use strict JavaScript for package glue code. Follow the existing compact style in `index.js`: CommonJS modules, single quotes, and minimal wrapper logic.

For C++, use two-space indentation, `PascalCase` for addon classes such as `NodeStormAddon`, and descriptive method names. Prefer Node-API abstractions from `node-addon-api` over direct V8 calls. Keep exported JavaScript-facing names stable and documented in README examples as features are added.

## Testing Guidelines

There is no test suite yet. When adding behavior, add tests under `test/` and wire `npm test` to the chosen runner. Prefer tests that exercise the public JavaScript API from `index.js` rather than only C++ internals. Name tests by behavior, for example `test/open-archive.test.js`.

Before submitting native changes, run a clean addon build and any new tests.

## Commit & Pull Request Guidelines

Recent history uses short subjects, such as `Initialize with basic codes`. Keep subjects concise and focused on one change.

Pull requests should include a summary, build/test results, linked issues when applicable, and usage notes for new API surface. Include screenshots only for documentation or tooling changes with visible output.

## Security & Configuration Tips

Do not commit build artifacts, `node_modules/`, or local IDE files. Treat sample MPQ archives as test fixtures only when licensing permits redistribution.

## Agent-Specific Instructions

When working in this repository, return results in Chinese unless the user explicitly requests another language.
