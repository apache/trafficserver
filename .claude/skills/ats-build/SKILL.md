---
name: ats-build
description: Use this skill for all actions related to building Trafficserver, running tests, or formatting code.
---

## Build Process

1. Determine build directory name, which is based on the current branch, tag, or commit SHA. This will be used for all builds on this branch/tag/commit.
2. If that build directory exists, skip this step, otherwise, initialize the build.
3. Determine the correct target to use.
4. Launch the build.

## Command Reference

```bash
ref=$(git symbolic-ref --short -q HEAD || git describe --tags --exact-match 2>/dev/null || git rev-parse --short HEAD)
BUILD_DIR="build-${ref##*/}"
```

### Initializing a Development Build

```bash
cmake --preset dev -B <build_dir> -DENABLE_AUTEST=ON
```

### Initializing an HTTP3 Development Build

```bash
cmake --preset ci-fedora-quiche -B <build_dir> -DENABLE_AUTEST=ON
```

### Initializing a Release Build

```bash
cmake --preset release -B <build_dir>
```

### Building a Code Target

```bash
cmake --build <build_dir> --target <executable_or_library_target>
```

### Running a Unit Test

```bash
ctest --test-dir <build_dir> [-R <filter>]
```

### Formatting Code

```bash
cmake --build <build_dir> --target format
```

### Running AuTests

Before running AuTests, run the following command as a safety check to prevent running with the wrong options:

```bash
grep "^AUTEST_OPTIONS" <build_dir>/CMakeCache.txt
```

If the options are incorrect, reconfigure the build, then run the AuTests:

```bash
cmake --build <build_dir> --target autest
```

#### Example: Configuring Build for Specific AuTest

```bash
cmake -B <build_dir> -DAUTEST_OPTIONS="-f <autest_name>"
```

### Installing a Build

Before installing, run the following command as a safety check to prevent installing to an undesired location:

```bash
grep "^CMAKE_INSTALL_PREFIX" <build_dir>/CMakeCache.txt
```

After checking, run:

```bash
cmake --install <build_dir>
```

## Key CMake Options

- `BUILD_EXPERIMENTAL_PLUGINS=ON` - Enable experimental plugins
- `ENABLE_QUICHE=ON` - QUIC/HTTP3 support
- `ENABLE_CRIPTS=ON` - Cripts scripting API
- `BUILD_REGRESSION_TESTING=ON` - Enable test suite
- `ENABLE_ASAN=ON` - Configure ASan instrumentation
