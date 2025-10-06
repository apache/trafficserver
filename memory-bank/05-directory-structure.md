# Apache Traffic Server - Directory Structure & Installation

## Directory Structure Summary

```
trafficserver/
├── ci/                    # CI/CD tools and configurations
├── configs/               # Default configuration files
├── contrib/               # Contributed tools and scripts
├── doc/                   # Documentation (Sphinx with RST)
├── example/               # Example plugins and configurations
├── ext/                   # External/extension components
├── include/               # Header files
│   ├── api/               # Plugin API headers
│   ├── iocore/            # I/O core headers
│   ├── proxy/             # Proxy headers
│   └── ts*/               # Various subsystem headers
├── lib/                   # Third-party libraries
├── plugins/               # Core and experimental plugins
├── src/                   # Source code
│   ├── iocore/            # I/O subsystem
│   ├── proxy/             # Proxy logic
│   ├── mgmt/              # Management system
│   ├── traffic_*/         # Command-line tools
│   └── ts*/               # Support libraries
├── tests/                 # Test suites
└── tools/                 # Development and maintenance tools
```

## Key Directories Explained

### Source Code (`src/`)
- **iocore/**: Core I/O subsystem (AIO, Cache, DNS, Event System, HostDB, Net, Utils)
- **proxy/**: Main proxy logic (HTTP/1.1, HTTP/2, HTTP/3, Headers, Logging, Shared)
- **mgmt/**: Management and administration system
- **traffic_***: Command-line utilities and tools
- **ts***: Support libraries (tscore, tsutil, tscpp, records, api)

### Headers (`include/`)
- **api/**: Plugin development API headers
- **iocore/**: I/O core system headers
- **proxy/**: Proxy subsystem headers
- **ts*/**: Various subsystem headers (tscore, tsutil, tscpp, etc.)

### Configuration (`configs/`)
- Default configuration files for all subsystems
- Schema files for YAML configurations
- Template files with `.in` extension for build-time substitution

### Plugins (`plugins/`)
- **Core plugins**: Stable, production-ready plugins
- **experimental/**: Experimental and development plugins

### Documentation (`doc/`)
- **Sphinx with reStructuredText (RST) documentation system**
- **Main sections organized as:**
  - **preface/**: Project introduction and overview
  - **getting-started/**: Installation and basic setup guides
  - **admin-guide/**: Administrator documentation
    - configuration/, files/, installation/, interaction/
    - logging/, monitoring/, performance/, plugins/
    - security/, storage/, tools/
  - **developer-guide/**: Developer documentation
    - api/, cache-architecture/, continuous-integration/
    - contributing/, core-architecture/, cripts/
    - debugging/, design-documents/, documentation/
    - internal-libraries/, introduction/, jsonrpc/
    - layout/, logging-architecture/, plugins/
    - release-process/, testing/
  - **appendices/**: Additional reference material
  - **release-notes/**: Version history and changes
- **Build system**: CMake integration for documentation generation
- **Internationalization**: Support for multiple locales
- **Templates and extensions**: Custom Sphinx extensions and templates

### Third-party Libraries (`lib/`)
- **catch2/**: Testing framework
- **fastlz/**: Fast compression library
- **ls-hpack/**: HPACK implementation
- **swoc/**: Solid Wall of Code utility library
- **yamlcpp/**: YAML parsing library

### Testing (`tests/`)
- Regression test suites
- AuTest automated testing framework
- Performance and stress tests

## Installation Layout

Default installation structure:
```
/usr/local/trafficserver/
├── bin/                   # Executables
├── etc/trafficserver/     # Configuration files
├── var/
│   ├── log/trafficserver/ # Log files
│   └── trafficserver/     # Runtime files
├── libexec/trafficserver/ # Plugins
└── lib/                   # Libraries
```

### Installation Paths
- **Binaries**: `/usr/local/trafficserver/bin/`
- **Configuration**: `/usr/local/trafficserver/etc/trafficserver/`
- **Plugins**: `/usr/local/trafficserver/libexec/trafficserver/`
- **Logs**: `/usr/local/trafficserver/var/log/trafficserver/`
- **Runtime data**: `/usr/local/trafficserver/var/trafficserver/`
- **Libraries**: `/usr/local/trafficserver/lib/`

### Customizable Installation
- Use `CMAKE_INSTALL_PREFIX` to change base installation directory
- Individual path components can be customized via CMake variables
- Layout follows standard Unix filesystem hierarchy principles
