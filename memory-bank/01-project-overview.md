# Apache Traffic Server - Project Overview

## Project Overview

**Apache Traffic Server (ATS)** is a high-performance, open-source HTTP/HTTPS caching proxy server and web application acceleration platform. It's designed as a building block for cloud services and large-scale web applications.

### Key Characteristics
- **Version**: 10.2.0 (current)
- **Language**: Primarily C++ (C++20 standard)
- **Build System**: CMake
- **License**: Apache License 2.0
- **Platform Support**: Linux, FreeBSD, macOS, with extensive CI across multiple distributions

## Architecture Overview

### Core Components

#### 1. **Traffic Server Core** (`src/traffic_server/`)
- Main proxy server executable
- Event-driven architecture
- Multi-threaded processing

#### 2. **I/O Core** (`src/iocore/`)
- **AIO**: Asynchronous I/O operations
- **Cache**: Disk and RAM caching system
- **DNS**: Asynchronous DNS resolution
- **Event System**: Event-driven engine foundation
- **HostDB**: Internal DNS cache
- **Net**: Network layer with QUIC support
- **Utils**: Core utilities

#### 3. **Proxy Logic** (`src/proxy/`)
- **HTTP**: HTTP/1.1 protocol implementation (`http/`)
- **HTTP/2**: HTTP/2 support (`http2/`)
- **HTTP/3**: HTTP/3 implementation (`http3/`)
- **Headers**: Header parsing and management (`hdrs/`)
- **Logging**: Flexible logging system (`logging/`)
- **Shared**: Common proxy components (`shared/`)

#### 4. **Management** (`src/mgmt/`)
- **JSONRPC**: RPC server for management and control
- Configuration management
- Administrative tools

#### 5. **Support Libraries** (`src/`)
- **tscore**: Base/core library
- **tsutil**: Utility functions
- **tscpp**: C++ API wrapper for plugin developers
- **records**: Configuration file library
- **api**: Plugin API

### Command Line Tools
Located in `src/` directory:
- **traffic_server**: Main proxy server
- **traffic_ctl**: Command line management tool
- **traffic_cache_tool**: Cache interaction utility
- **traffic_crashlog**: Crash handling helper
- **traffic_layout**: Directory structure information
- **traffic_logcat**: Binary log to text converter
- **traffic_logstats**: Log parsing and metrics
- **traffic_top**: Statistics monitoring (like top)
- **traffic_via**: Via header decoder
- **traffic_quic**: QUIC-related utilities

## Key Features

### Performance Features
- **Multi-threaded**: Event-driven architecture
- **Caching**: Sophisticated disk and memory caching
- **Connection pooling**: Efficient connection reuse
- **Compression**: Built-in content compression
- **Load balancing**: Multiple load balancing algorithms

### Protocol Support
- **HTTP/1.1**: Full support
- **HTTP/2**: Complete implementation
- **HTTP/3**: QUIC-based HTTP/3 support using Quiche Rust library
- **TLS/SSL**: Modern TLS support including TLS 1.3
- **WebSocket**: WebSocket proxying

### Advanced Features
- **Plugin system**: Extensive plugin architecture
- **Lua scripting**: Embedded Lua for custom logic
- **Statistics**: Comprehensive metrics and monitoring
- **Logging**: Flexible, high-performance logging
- **Health checks**: Built-in health monitoring

## Community & Resources
- **Website**: https://trafficserver.apache.org/
- **Wiki**: https://cwiki.apache.org/confluence/display/TS/
- **Mailing Lists**: users@trafficserver.apache.org
- **GitHub**: https://github.com/apache/trafficserver
- **CI**: https://ci.trafficserver.apache.org/
