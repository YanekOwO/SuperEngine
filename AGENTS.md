# Repository Guidelines

## Project Structure & Module Organization

RLVM is a C++20 interpreter for RealLive and Siglus games. Engine implementations live in `src/libreallive/` and `src/libsiglus/`; `src/vm/`, `src/m6/`, and `src/srbind/` contain the VM, script compiler, and native bindings. Shared runtime code lives in `src/core/`, with SDL rendering, input, and audio under `src/systems/`.

Tests and fixtures live in `test/`, including engine-specific subdirectories. `resources/` contains application artwork, `examples/` contains sample scripts, `tools/` contains asset utilities, and `vendor/` contains third-party dependencies.

## Build, Test, and Development Commands

Install dependencies described in `README.md`; presets require CMake 3.24+ and Ninja. Initialize submodules before configuring:

```sh
git submodule update --init --recursive
cmake --preset debug
cmake --build --preset debug -j $(nproc)
ctest --preset debug
```

Debug builds include unit tests. Release builds omit tests:

```sh
cmake --preset release
cmake --build --preset release -j $(nproc)
./build/release/rlvm --engine siglus /path/to/game
```

Build trees live under `build/<preset>/`. Adjust parallelism to available memory.

## Coding Style & Naming Conventions

Use two-space indentation and the Chromium-based `.clang-format` configuration. Check changed C++ files with `clang-format --dry-run --Werror <files>`. CI also runs `clang-tidy` using the release compilation database.

Follow surrounding conventions: `snake_case` filenames, `PascalCase` types and methods, and trailing underscores for members where established. Preserve existing license headers and keep changes within the relevant subsystem.

Describe behavior and symbols in production/test comments and diagnostics. Keep filename, path, and line-number provenance in documentation because those references become stale. Use paths in code when required for behavior, such as includes or file-based fixtures.

## Serialization

Serialization support for game archives is under development and incomplete. When changing serialization code, backward compatibility with existing archives is not required. Keep serialized classes unversioned: do not add class versioning, and remove existing versioning and associated compatibility branches from classes you modify to keep the code simple.

## Testing Guidelines

Tests use GoogleTest and GoogleMock. Name test files `*_unittest.cpp` and cases `TEST_F(Fixture, DescriptiveBehavior)`. Add deterministic regression coverage for changed behavior and edge cases.

Test fixtures must inherit directly and only from `::testing::Test`. Do not reuse code through fixture inheritance. Extract reusable helper classes used through composition, or reusable functions.

Each unit test case must test exactly one behavior. Add brief code comments in every case describing the test setup, the behavior under test, and the expected outcome.

Run focused tests from the debug build directory:

```sh
(cd build/debug && ./unittest --gtest_filter='GCTest.*')
```

Run `ctest --preset debug` before submitting. Fixtures and generated audio inputs are prepared during the test build.

## Commit & Pull Request Guidelines

History favors short imperative subjects, such as `Refactor TextWindow class` or `bug(memory): Fix ...`; use a subsystem prefix when helpful.

Keep PRs focused. Describe the problem, resulting behavior, and validation commands; link relevant issues. Include screenshots for visible rendering changes and measurements for performance changes. Ensure CI checks pass.

## Local Configuration & Game Assets

Put machine-specific compiler and dependency settings in ignored `CMakeUserPresets.json`. Use legally obtained game assets and keep proprietary assets, saves, dumps, and machine-specific paths out of commits.
