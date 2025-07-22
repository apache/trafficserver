# Apache Traffic Server - Build System & Configuration

## Build System & Configuration

### CMake Build System (Version 10+)
```bash
# Basic build
cmake -B build
cmake --build build

# With options
cmake -B build -DCMAKE_INSTALL_PREFIX=/opt/ats -DBUILD_EXPERIMENTAL_PLUGINS=ON

# Using presets
cmake --preset dev
```

### Key Build Options
- `BUILD_EXPERIMENTAL_PLUGINS`: Enable experimental plugins
- `BUILD_REGRESSION_TESTING`: Build test suite
- `ENABLE_QUICHE`: QUIC support via quiche
- `ENABLE_LUAJIT`: Lua scripting support
- `ENABLE_CRIPTS`: Enable the Cripts scripting API
- `ENABLE_JEMALLOC`/`ENABLE_MIMALLOC`: Alternative memory allocators
- `ENABLE_HWLOC`: Hardware locality support
- `ENABLE_POSIX_CAP`: POSIX capabilities
- `ENABLE_DOCS`: Documentation building
- `ENABLE_AUTEST`: Automated testing framework

### Dependencies
**Required:**
- CMake 3.20+
- C++20 compatible compiler (GCC 10+, LLVM/Clang 12+)
- OpenSSL/BoringSSL/AWS-LC
- PCRE2 (Perl Compatible Regular Expressions)
- zlib

**Optional but Recommended:**
- hwloc (hardware locality)
- libcap (POSIX capabilities)
- ncurses (for traffic_top)
- jemalloc/mimalloc (memory allocators)
- libfmt (for Cripts)

## Configuration System

### Configuration Files (`configs/`)
- **records.yaml**: Main configuration (YAML format)
- **cache.config**: Cache rules
- **remap.config**: URL remapping rules
- **parent.config**: Parent proxy configuration
- **ssl_multicert.config**: SSL certificate configuration
- **plugin.config**: Plugin loading configuration
- **ip_allow.yaml**: IP access control
- **logging.yaml**: Logging configuration
- **storage.config**: Storage configuration

### Configuration Management
- JSONRPC API for runtime configuration
- `traffic_ctl` command-line interface
- Hot reloading for many configuration changes

## Development Environment Setup

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt-get install cmake ninja-build pkg-config gcc g++ \
    libssl-dev libpcre3-dev libcap-dev libhwloc-dev \
    libncurses5-dev libcurl4-openssl-dev zlib1g-dev

# macOS (Homebrew)
brew install cmake ninja pkg-config openssl pcre
```

### Quick Start
```bash
git clone https://github.com/apache/trafficserver.git
cd trafficserver
cmake --preset default
cmake --build build-default
cmake --install build-default
```

## Recent Changes & Migration Notes

### CMake Migration (Version 10+)
- Transitioned from autotools to CMake
- New build commands and options
- Preset system for common configurations
- Out-of-source builds required

### Modern C++ Adoption
- C++20 standard requirement
- Modern compiler requirements (GCC 10+ / LLVM 12+)
- Enhanced type safety and performance
