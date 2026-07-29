---
name: ats-build
description: Basic skills; formatting, compiling, testing, installing.
---

## Build Process

1. Which `cmake --list-presets`?
  * Need HTTP3? `ci-fedora-quiche`
  * Not sure? Ask.
2. No build directory yet? Configure one.
3. Which target?
4. Execute.

## Reference

### Configure Development Build

```bash
cmake --preset <name> [-B <build_dir>] -DENABLE_AUTEST=ON
```

### Build Code Target

```bash
cmake --build <build_dir> --target <executable_or_library_target>
```

### Run Unit Test

```bash
ctest --test-dir <build_dir> [-R <filter>]
```

### Format

```bash
cmake --build <build_dir> --target format
```

### Run AuTest

Options correct? Check:

```bash
grep "^AUTEST_OPTIONS" <build_dir>/CMakeCache.txt
```

Correct? Run:

```bash
cmake --build <build_dir> --target autest
```

#### Example: Configure Build for Specific AuTest

```bash
cmake -B <build_dir> -DAUTEST_OPTIONS="-f <autest_name>"
```

### Install

Install location OK? Check:

```bash
grep "^CMAKE_INSTALL_PREFIX" <build_dir>/CMakeCache.txt
```

Correct? Run:

```bash
cmake --install <build_dir>
```

## Key CMake Options

- `BUILD_EXPERIMENTAL_PLUGINS=ON` - Enable experimental plugins
- `ENABLE_QUICHE=ON` - QUIC/HTTP3 support
- `ENABLE_CRIPTS=ON` - Cripts scripting API
- `BUILD_REGRESSION_TESTING=ON` - Enable test suite
- `ENABLE_ASAN=ON` - Configure ASan instrumentation
