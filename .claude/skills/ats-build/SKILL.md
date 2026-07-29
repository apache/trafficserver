---
name: ats-build
description: Use this skill for all actions related to building Trafficserver, running tests, or formatting code.
---

There are two development modes for ATS: host local, and docker mount. If a Docker container named `ats-dev` exists, then the source
code is bind mounted into that container. If that

## Build Process

1. Determine whether build mode is host local or docker mount.
2. Determine build directory name, which is based on current branch or tag. This will be used for all builds on this branch/tag.
3. If that build directory exists, skip this step, otherwise, initialize the build.
4. Determine the correct target to use.
5. Launch the build.

## Command Reference

The commands in this reference are shown without the `docker exec` prefix. If in docker mount mode, wrap each of the commands in `docker exec`

### Determining Build Directory Name

```bash
ref=$(git symbolic-ref --short -q HEAD || git describe --tags)
BUILD_DIR="build-${ref##*/}"
```

### Initializing a Development Build

```bash
ATS_BUILD=1 cmake --preset dev -B <build_dir> -DENABLE_AUTEST=ON"
```

### Initializing an HTTP3 Development Build

```bash
ATS_BUILD=1 cmake --preset ci-fedora-quiche -B <build_dir> -DENABLE_AUTEST=ON
```

### Initializing a Release Build

```bash
ATS_BUILD=1 cmake --preset release -B <build_dir>
```

### Building a Code Target

```bash
ATS_BUILD=1 cmake --build <build_dir> --target <executable_or_library_target>
```

### Running a Unit Test

```bash
ATS_BUILD=1 ctest --test-dir <build_dir> [-R <filter>]
```

### Formatting Code

```bash
ATS_BUILD=1 cmake --build <build_dir> --target format
```

### Running AuTests

Before running AuTests, run the following command as a safety check to prevent running with the wrong options:

```bash
grep "^AUTEST_OPTIONS" <build_dir>/CMakeCache.txt
```

If the options are incorrect, reconfigure the build, then run the AuTests:

```bash
ATS_BUILD=1 cmake --build <build_dir> --target autest
```

#### Example: Configuring Build for Specific AuTest

```bash
ATS_BUILD=1 cmake -B <build_dir> -DAUTEST_OPTIONS="-f <autest_name>"
```

### Installing a Build

Before installing, run the following command as a safety check to prevent installing to an undesired location:

```bash
grep "^CMAKE_INSTALL_PREFIX" <build_dir>/CMakeCache.txt
```

After checking, run:

```bash
ATS_BUILD=1 cmake --install <build_dir>
```

## Key CMake Options

- `BUILD_EXPERIMENTAL_PLUGINS=ON` - Enable experimental plugins
- `ENABLE_QUICHE=ON` - QUIC/HTTP3 support
- `ENABLE_CRIPTS=ON` - Cripts scripting API
- `BUILD_REGRESSION_TESTING=ON` - Enable test suite
- `ENABLE_ASAN=ON` - Configure ASan instrumentation
