# Apache Traffic Server - Plugin System

## Plugin System

### Core Plugins (`plugins/`)
Stable, production-ready plugins:
- **authproxy**: Authentication proxy
- **background_fetch**: Background content fetching
- **cache_promote**: Cache promotion logic
- **cache_range_requests**: Range request handling
- **cachekey**: Cache key manipulation
- **certifier**: Certificate management
- **compress**: Content compression
- **conf_remap**: Configuration-based remapping
- **escalate**: Request escalation
- **esi**: Edge Side Includes processing
- **generator**: Content generation
- **header_rewrite**: Header manipulation
- **healthchecks**: Health monitoring
- **ja3_fingerprint**: JA3 fingerprinting
- **libloader**: Dynamic library loading
- **lua**: Lua scripting support
- **multiplexer**: Request multiplexing
- **origin_server_auth**: Origin server authentication
- **prefetch**: Content prefetching
- **regex_remap**: Regular expression remapping
- **regex_revalidate**: Regular expression revalidation
- **remap_purge**: Remapping with purge capability
- **remap_stats**: Remapping statistics
- **server_push_preload**: HTTP/2 server push preload
- **slice**: Content slicing
- **statichit**: Static content handling
- **stats_over_http**: HTTP statistics interface
- **tcpinfo**: TCP connection information
- **traffic_dump**: Traffic dumping for analysis
- **webp_transform**: WebP image transformation
- **xdebug**: Extended debugging

### Experimental Plugins (`plugins/experimental/`)
- Cutting-edge features under development
- May become core plugins or be deprecated

## Plugin Architecture

### Hook-based Plugin System
- Multiple plugin types (remap, global, etc.)
- C++ and Lua plugin support
- Event-driven plugin execution

### Plugin Configuration
- **plugin.config**: Plugin loading configuration
- Per-plugin configuration files
- Runtime plugin management via JSONRPC API

### Plugin Development
- C++ API wrapper for plugin developers (`tscpp`)
- Plugin API headers (`include/api/`)
- Example plugins (`example/plugins/`)
