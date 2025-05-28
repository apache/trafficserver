# Apache Traffic Server Docker Test Environment

This directory contains a Docker-based test environment for Apache Traffic Server (ATS). It provides a quick way to build, run, and test ATS functionality using containers.

## Directory Structure

```
docker/
├── Dockerfile              # Builds ATS from source on Rocky Linux 8
├── docker-compose.yml      # Defines services: trafficserver and test_client
├── config/                 # Main configuration files
│   ├── records.yaml       # Core ATS configuration
│   ├── remap.config       # URL remapping rules
│   └── ip_allow.yaml      # Access control configuration
└── proxy/                 # Runtime configuration and data
    ├── etc/trafficserver  # Live configuration files
    ├── var/cache         # Cache storage
    └── var/log           # Log files
```

## Configuration Files

### records.yaml
Key settings:
- Forward proxy mode enabled
- Port 8080 for HTTP traffic
- Caching enabled with aggressive settings
- Debug logging enabled

### remap.config
Contains URL remapping rules:
- Maps `foo.com` to `example.com` for testing
- Demonstrates basic remapping functionality

### ip_allow.yaml
Access control configuration:
- Allows connections from all IPs (for testing)
- Controls which IPs can access different ports

## Quick Start

1. Build and start the containers:
   ```bash
   cd docker
   docker-compose up -d
   ```

2. Test basic functionality:
   ```bash
   # Test the proxy with remapping
   docker-compose exec test_client curl -v -H "Host: foo.com" http://trafficserver:8080/

   # Test caching (notice the Age header)
   docker-compose exec test_client curl -I -H "Host: foo.com" http://trafficserver:8080/
   ```

## What to Look For

### 1. Successful Proxy Operation
- Response status should be 200 OK
- Server header should show `ATS/9.2.0`
- Content should be from example.com

### 2. Caching Behavior
Check these headers in responses:
- `Age`: Shows time in cache (0 for fresh responses)
- `Date`: When the response was originally received
- `Cache-Control`: Caching directives
- `Last-Modified`: Original content timestamp

Example of a cached response:
```http
HTTP/1.1 200 OK
Server: ATS/9.2.0
Date: Thu, 20 Mar 2025 06:04:28 GMT
Age: 60                    # Content has been in cache for 60 seconds
Cache-Control: max-age=3236
```

### 3. URL Remapping
Verify that requests to foo.com are mapped to example.com:
```bash
# Should show example.com's content
docker-compose exec test_client curl -v -H "Host: foo.com" http://trafficserver:8080/
```

### 4. Cache Bypass
Test cache bypass using query parameters:
```bash
# Force a fresh response
docker-compose exec test_client curl -v -H "Host: foo.com" "http://trafficserver:8080/?nocache=$(date +%s)"
```

## Logs and Debugging

1. View ATS logs:
   ```bash
   # Error logs
   docker-compose exec trafficserver tail -f /opt/ts/var/log/trafficserver/error.log
   
   # Traffic logs
   docker-compose exec trafficserver tail -f /opt/ts/var/log/trafficserver/traffic.out
   ```

2. Check container status:
   ```bash
   docker-compose ps
   ```

3. View container logs:
   ```bash
   docker-compose logs -f trafficserver
   ```

## Cleanup

To stop and remove containers:
```bash
docker-compose down
```

To also remove built images:
```bash
docker-compose down --rmi all
```
