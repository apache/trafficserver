# HRW4U - Header Rewrite for You

[![Apache License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Python](https://img.shields.io/badge/python-3.11+-blue.svg)](https://python.org)

HRW4U is a domain-specific language and compiler toolchain for Apache Traffic Server's `header_rewrite` plugin, providing a more readable and maintainable alternative to writing header rewrite rules directly.

## Tools Overview

### `hrw4u` - Forward Compiler
Compiles HRW4U source files into Apache Traffic Server `header_rewrite` configuration.

**Input**: Human-readable HRW4U syntax
**Output**: Native `header_rewrite` plugin rules

### `u4wrh` - Reverse Compiler
Decompiles existing `header_rewrite` rules back into HRW4U source code.

**Input**: Native `header_rewrite` plugin rules
**Output**: Human-readable HRW4U syntax

## Requirements

### Python Tools (hrw4u, u4wrh)
- **Python 3.11+** (uses modern type annotations and performance features)
- **ANTLR4** for grammar parsing
- **uv** (for Python environment management)

### C++ Native Parser (src/hrw4u)

ATS includes a C++ ANTLR4-based parser that lets the `header_rewrite` plugin
load `.hrw4u` files directly. Building it requires:

- **ANTLR4 C++ runtime** (headers and shared library)
- **ANTLR4 tool** (`antlr4` command, same major.minor version as the runtime)

#### macOS (Homebrew)
```bash
brew install antlr4-cpp-runtime antlr4
```

#### Fedora/RHEL
```bash
dnf install antlr4-cpp-runtime-devel antlr4
```

#### Ubuntu/Debian (from source recommended)

The distro packages often have version mismatches between the tool and runtime,
and the shared library may be built with a different C++ ABI. Building from
source is recommended:

```bash
# Download ANTLR4 C++ runtime source (match JAR version)
# https://github.com/antlr/antlr4/releases

cd antlr4-cpp-runtime-<version>
cmake -B build \
  -DCMAKE_INSTALL_PREFIX=/opt/antlr4 \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DANTLR4_INSTALL=ON
cmake --build build -j$(nproc)
cmake --install build
```

If you are building ATS with a non-default compiler toolchain, build the
ANTLR4 runtime with the same compilers to avoid ABI mismatches:

```bash
cmake -B build \
  -DCMAKE_C_COMPILER=/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/llvm/bin/clang++ \
  -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
  -DCMAKE_INSTALL_PREFIX=/opt/antlr4 \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DANTLR4_INSTALL=ON
cmake --build build -j$(nproc)
cmake --install build
```

Then pass the install prefix when configuring ATS:
```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/antlr4 ...
```

If the `antlr4` tool is not on `PATH`, set it explicitly:
```bash
cmake -B build -DANTLR4_EXECUTABLE=/opt/antlr4/bin/antlr4 ...
```

## Development Setup

### 1. Clone and Setup Environment
```bash
git clone https://github.com/apache/trafficserver.git
cd trafficserver/tools/hrw4u

# Install uv if not already installed
# See: https://docs.astral.sh/uv/getting-started/installation/

# Create virtual environment and install dependencies
uv sync --all-extras
```

### 2. Build the Package
```bash
# Generate parsers and build packages
make

# Create distributable wheel
make package
```

### 3. Install for Development
```bash
# Install in development mode
uv pip install -e .

# Or install the built wheel
uv pip install dist/hrw4u-*.whl
```

## Testing

```bash
# Run all tests
make test

# Run specific test categories
uv run pytest -m examples    # Documentation examples
uv run pytest -m conds       # Condition tests
uv run pytest -m ops         # Operator tests
uv run pytest -m reverse     # Reverse compilation tests
```

## Usage

### Forward Compilation (hrw4u)

```bash
# Compile HRW4U source to header_rewrite rules
hrw4u input.hrw4u > output.conf

# From stdin
cat input.hrw4u | hrw4u > output.conf

# Show parse tree (debugging)
hrw4u --ast input.hrw4u

# Enable debug output
hrw4u --debug input.hrw4u
```

**Options:**
- `--hrw` - Produce header_rewrite output (default)
- `--ast` - Show ANTLR parse tree
- `--debug` - Enable debug tracing

### Reverse Compilation (u4wrh)

```bash
# Decompile header_rewrite rules to HRW4U source
u4wrh rules.conf > output.hrw4u

# From stdin
cat rules.conf | u4wrh > output.hrw4u

# Show parse tree (debugging)
u4wrh --ast rules.conf

# Enable debug output
u4wrh --debug rules.conf
```

**Options:**
- `--hrw4u` - Produce HRW4U source output (default)
- `--ast` - Show ANTLR parse tree
- `--debug` - Enable debug tracing

## Example

**HRW4U Source** (`example.hrw4u`):
```
REMAP {
    if inbound.ip in {10.0.0.0/8, 192.168.0.0/16} {
        inbound.req.X-IP = "{inbound.ip}";
    }
}
```

**Generated header_rewrite rules**:
```bash
hrw4u example.hrw4u
```
```
cond %{REMAP_PSEUDO_HOOK} [AND]
cond %{IP:CLIENT} {10.0.0.0/8,192.168.0.0/16}
    set-header X-IP "%{IP:CLIENT}"
```

**Generated hrw4u script from header_rewrite rules**:
```bash
hrw4u example.hrw4u | u4wrh
```
```
REMAP {
    if inbound.ip in {10.0.0.0/8, 192.168.0.0/16} {
        inbound.req.X-IP = "{inbound.ip}";
    }
}
```

Passing the output from hrw4u back into u4wrh brings back the original script again!

## Build System

The project uses a hybrid build system:

- **GNU Make** - Grammar generation and package building
- **pyproject.toml** - Modern Python packaging configuration
- **ANTLR4** - Grammar compilation to Python parsers

### Key Make Targets

```bash
make           # Build everything (default)
make clean     # Remove build artifacts
make test      # Run test suite
make package   # Create distributable wheel
```

## Project Structure

```
tools/hrw4u/
├── src/                  # Python source code
│   ├── common.py        # Shared utilities and patterns
│   ├── types.py         # Type definitions and dataclasses
│   ├── symbols.py       # Symbol resolution engine
│   └── ...              # Additional modules
├── scripts/             # Command-line entry points
│   ├── hrw4u           # Forward compiler script
│   └── u4wrh           # Reverse compiler script
├── grammar/             # ANTLR4 grammar definitions
├── tests/               # Comprehensive test suite
└── pyproject.toml       # Modern Python package configuration
```

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](../../LICENSE) for details.

---

*Part of the [Apache Traffic Server](https://trafficserver.apache.org/) project.*
