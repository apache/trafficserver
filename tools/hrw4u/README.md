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

- **Python 3.11+** (uses modern type annotations and performance features)
- **ANTLR4** for grammar parsing
- **pyenv** (recommended for development)

## Development Setup

### 1. Clone and Setup Environment
```bash
git clone https://github.com/apache/trafficserver.git
cd trafficserver/tools/hrw4u

# Create pyenv virtual environment
pyenv virtualenv 3.11 hrw4u
pyenv activate hrw4u
pip install -r requirements.txt
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
pip install -e .

# Or install the built wheel
pip install dist/hrw4u-*.whl
```

## Testing

```bash
# Run all tests
make test

# Run specific test categories
pytest -m examples    # Documentation examples
pytest -m conds       # Condition tests
pytest -m ops          # Operator tests
pytest -m reverse      # Reverse compilation tests
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
