# Configuration Parsing Library

This directory contains a shared configuration parsing and marshalling framework
used by various ATS components including `traffic_server` and `traffic_ctl`.

## Architecture Overview

Each configuration file type (ssl_multicert, logging, sni, etc.) follows the
same pattern:

```
include/config/<config_name>.h   - Header with data types, parser, marshaller
src/config/<config_name>.cc      - Implementation
```

### Components

For each configuration type, there are three main components:

1. **Entry struct** - A POD (plain old data) type representing a single
   configuration entry (e.g., `SSLMultiCertEntry`).

2. **Parser class** - Reads configuration files and returns parsed entries.
   Supports both YAML and legacy formats with automatic format detection.

3. **Marshaller class** - Serializes configuration to YAML or JSON format.

### Example: ssl_multicert

```
include/config/ssl_multicert.h
├── SSLMultiCertEntry      - Single certificate entry
├── SSLMultiCertConfig     - Vector of entries (type alias)
├── ConfigResult<T>        - Parse result with error handling
├── SSLMultiCertParser     - Parser for .yaml and .config formats
└── SSLMultiCertMarshaller - Serializer to YAML/JSON
```

## Usage

### Parsing a Configuration File

```cpp
#include "config/ssl_multicert.h"

config::SSLMultiCertParser parser;
auto result = parser.parse("/path/to/ssl_multicert.yaml");

if (!result.ok()) {
    // Handle error - result.errata contains error details
    return;
}

for (const auto& entry : result.value) {
    // Use entry.ssl_cert_name, entry.dest_ip, etc.
}
```

### Parsing from a String

```cpp
std::string content = "ssl_multicert:\n  - ssl_cert_name: server.pem\n";
auto result = parser.parse_string(content, "config.yaml");
```

### Marshalling to YAML/JSON

```cpp
config::SSLMultiCertMarshaller marshaller;
std::string yaml = marshaller.to_yaml(result.value);
std::string json = marshaller.to_json(result.value);
```

## Format Detection

The parser automatically detects the configuration format:

1. **By file extension**: `.yaml`/`.yml` → YAML, `.config` → Legacy
2. **By content inspection**: Looks for `ssl_multicert:` (YAML) vs `key=value` (Legacy)

## Adding a New Configuration Type

To add support for a new configuration (e.g., `logging.yaml`):

1. Create `include/config/logging.h`:
   - Define `LoggingEntry` struct with fields matching the config
   - Define `LoggingConfig = std::vector<LoggingEntry>`
   - Declare `LoggingParser` and `LoggingMarshaller` classes

2. Create `src/config/logging.cc`:
   - Implement YAML parsing using yaml-cpp
   - Implement legacy format parsing if applicable
   - Implement YAML and JSON marshalling

3. Update `src/config/CMakeLists.txt`:
   - Add `logging.cc` to the library sources
   - Add header to `CONFIG_PUBLIC_HEADERS`

4. Integrate with consumers (traffic_ctl, traffic_server, etc.)

## Error Handling

All parsers return `ConfigResult<T>` which contains:

- `value` - The parsed configuration (valid even on partial failure)
- `errata` - Error/warning information using `swoc::Errata`
- `ok()` - Returns true if no errors occurred

```cpp
if (!result.ok()) {
    if (!result.errata.empty()) {
        std::cerr << result.errata.front().text() << std::endl;
    }
}
```

## Dependencies

- **yaml-cpp** - YAML parsing
- **libswoc** - Error handling (`swoc::Errata`)

## Running Unit Tests

Build with testing enabled and run:

```bash
cmake -B build -DBUILD_TESTING=ON ...
cmake --build build --target test_tsconfig
ctest --test-dir build -R test_tsconfig
```
