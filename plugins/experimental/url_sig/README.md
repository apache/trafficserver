# url_sig — Signed URL Plugin

## Overview

The url_sig plugin validates a cryptographic signature embedded in request
URLs. Requests with an invalid or missing signature are rejected with HTTP 403
(Forbidden) or redirected with HTTP 302 (Moved Temporarily), depending on
configuration. When the signature is valid the signing query string is stripped
so the origin sees a clean URL and the cache can serve hits normally.

The signature is an HMAC (SHA-1 or MD5) computed over selected parts of the
URL using a shared secret key. A signing portal generates signed URLs; the
edge cache validates them. Signed URLs do not replace DRM.

## Architecture

The plugin is split into cache-agnostic core logic and a thin ATS adapter:

| File | Purpose |
|------|---------|
| `url_sig.h` | Public header — config structs, constants, function declarations. No ATS dependencies. |
| `url_sig_config.cc` | Config file parser. Reads from `std::istream`. |
| `url_sig_verify.cc` | URL validation — parameter extraction, HMAC computation, signature comparison. |
| `url_sig.cc` | ATS remap plugin glue — implements `TSRemap*` hooks, delegates to core. |
| `test_url_sig.cc` | Catch2 unit tests for core logic. |
| `genkeys.go` | Go tool to generate a config file with random keys. |
| `sign.go` | Go tool to sign URLs (produces a curl command). |

## Building

### Plugin (with ATS)

Enable the plugin at CMake configure time:

```bash
cmake --preset dev -DENABLE_URL_SIG=ON -DBUILD_TESTING=ON
cmake --build build-dev --target url_sig
```

### Unit Tests

```bash
cmake --build build-dev --target test_url_sig
./build-dev/plugins/experimental/url_sig/test_url_sig
```

### Go Tools

The signing tools are standalone Go files. Run them directly with `go run`:

```bash
go run genkeys.go > /etc/trafficserver/url_sig.config
go run sign.go --url http://example.com/path --useparts 1 --algorithm 1 \
    --duration 3600 --keyindex 3 --key YOUR_SECRET_KEY
```

No `go.mod` needed — each file has `//go:build ignore`.

## Configuration

### Config File Format

The config file is a simple `key = value` text file. Lines starting with `#`
are comments. The file must contain at least one key and an `error_url` line.

```
# Shared signing keys (up to 16, index 0-15).
key0 = YwG7iAxDo6Gaa38KJOceV4nsxiAJZ3DS
key1 = nLE3SZKRgaNM9hLz_HnIvrCw_GtTUJT1
key2 = YicZbmr6KlxfxPTJ3p9vYhARdPQ9WJYZ
key3 = DTV4Tcn046eM9BzJMeYrYpm3kbqOtBs7
...

# Error behavior: "403" to deny, or "302 <url>" to redirect.
error_url = 403
```

#### Supported Keys

| Key | Value | Description |
|-----|-------|-------------|
| `key0` .. `key15` | string (max 255 chars) | Shared HMAC signing keys. |
| `error_url` | `403` or `302 <redirect_url>` | Response for failed validation. |
| `sig_anchor` | string | Anchor name for path-parameter mode (e.g. `urlsig`). |
| `excl_regex` | regex pattern | URLs matching this pattern skip signature validation. |
| `ignore_expiry` | `true` | Disable expiration checking (for debugging only). |
| `url_type` | `pristine` | Use the pristine (pre-remap) URL for validation. |

### Generate a Config File

```bash
go run genkeys.go > /etc/trafficserver/url_sig.config
```

Or create one manually. Keys should be random strings shared only between the
signing portal and the edge caches.

## Plugin Setup

### remap.config

Add the plugin to the remap rule for the domain you want to protect:

```
map http://cdn.example.com http://origin.example.com \
    @plugin=url_sig.so @pparam=url_sig.config
```

The config file path is relative to `etc/trafficserver/` unless it starts
with `/`.

#### Optional: Pristine URL Mode

To validate against the pre-remap URL, add `pristineurl` as a second
parameter or set `url_type = pristine` in the config file:

```
map http://cdn.example.com http://origin.example.com \
    @plugin=url_sig.so @pparam=url_sig.config @pparam=pristineurl
```

### Reload

After editing `remap.config` or the signing config:

```bash
traffic_ctl config reload
```

## Signing Parameters

The signing parameters are appended to the URL as a query string (default) or
embedded in the path (path-parameter mode). Parameters:

| Param | Name | Description |
|-------|------|-------------|
| `C` | Client IP | Optional. Locks signature to a specific client IP (IPv4 or IPv6). |
| `E` | Expiration | Required. Seconds since Unix epoch when the signature expires. |
| `A` | Algorithm | Required. `1` = HMAC-SHA1, `2` = HMAC-MD5. |
| `K` | Key Index | Required. Index (0-15) of the key in the config file. |
| `P` | Parts | Required. Bitmask of URL parts to include in signing (see below). |
| `S` | Signature | Required. Hex-encoded HMAC. Must be last parameter. |

### Parts Mask

The URL (minus scheme) is split by `/`. Each character in the parts string
controls whether that segment is included in the signed string:

- `1` — include this part and all path parts
- `0110` — skip fqdn, include parts 1 and 2, skip the rest
- `01` — skip fqdn, include everything else

If the parts string is shorter than the number of URL segments, the last
character repeats for remaining segments.

### Query String Mode (Default)

Parameters are appended as a standard query string:

```
http://cdn.example.com/path/file.ts?E=1700000000&A=1&K=3&P=1&S=9e2828d5...
```

### Path Parameter Mode

Parameters are base64-encoded and embedded in the path before the filename.
Use the `sig_anchor` config option and `--pathparams --siganchor` flags:

```
http://cdn.example.com/path;urlsig=O0U9MTQ2.../file.ts?appid=2
```

Application query parameters follow the filename and are never part of the
signed string.

## Signing a URL

### Using the Go Tool

**Basic query string signing:**

```bash
go run sign.go \
    --url http://cdn.example.com/video/segment.ts \
    --useparts 1 \
    --algorithm 1 \
    --duration 3600 \
    --keyindex 3 \
    --key DTV4Tcn046eM9BzJMeYrYpm3kbqOtBs7
```

Output:

```
curl -s -o /dev/null -v --max-redirs 0 'http://cdn.example.com/video/segment.ts?E=1700003600&A=1&K=3&P=1&S=a1b2c3d4...'
```

**With client IP restriction:**

```bash
go run sign.go \
    --url http://cdn.example.com/video/segment.ts \
    --useparts 1 \
    --algorithm 1 \
    --duration 3600 \
    --keyindex 3 \
    --key DTV4Tcn046eM9BzJMeYrYpm3kbqOtBs7 \
    --client 10.10.10.10
```

**Path parameter mode with sig anchor:**

```bash
go run sign.go \
    --url "http://cdn.example.com/vod/t/prog_index.m3u8?appid=2&t=1" \
    --useparts 1 \
    --algorithm 1 \
    --duration 86400 \
    --keyindex 3 \
    --key kSCE1_uBREdGI3TPnr_dXKc9f_J4ZV2f \
    --pathparams \
    --siganchor urlsig
```

**Through a proxy:**

```bash
go run sign.go \
    --url http://cdn.example.com/ \
    --useparts 1 \
    --algorithm 1 \
    --duration 60 \
    --keyindex 0 \
    --key mykey \
    --proxy http://localhost:8080
```

**Verbose mode (shows signed string and digest on stderr):**

```bash
go run sign.go --verbose --url http://cdn.example.com/ \
    --useparts 1 --algorithm 1 --duration 60 --keyindex 0 --key mykey
```

### sign.go Flags

| Flag | Required | Default | Description |
|------|----------|---------|-------------|
| `--url` | yes | | Full URL to sign |
| `--useparts` | yes | | Parts bitmask string |
| `--duration` | yes | | Signature lifetime in seconds |
| `--key` | yes | | Signing key string |
| `--keyindex` | yes | `0` | Key index (0-15) |
| `--algorithm` | no | `1` | 1=HMAC-SHA1, 2=HMAC-MD5 |
| `--client` | no | | Lock to client IP |
| `--pathparams` | no | `false` | Use path parameter mode |
| `--siganchor` | no | | Anchor name for path params |
| `--proxy` | no | | Proxy URL:port for curl output |
| `--verbose` | no | `false` | Print signing details to stderr |

## Debugging

Enable debug logging in `records.yaml`:

```yaml
records:
  diags:
    debug:
      enabled: 1
      tags: url_sig
```

Then reload:

```bash
traffic_ctl config reload
```

- Debug output goes to `traffic.out` / `diags.log`.
- Failed signature checks are logged to `error.log`.

## Walkthrough Example

1. **Generate keys:**

   ```bash
   go run genkeys.go > /etc/trafficserver/url_sig.config
   ```

2. **Configure remap** (`remap.config`):

   ```
   map http://cdn.example.com http://origin.example.com \
       @plugin=url_sig.so @pparam=url_sig.config
   ```

3. **Reload ATS:**

   ```bash
   traffic_ctl config reload
   ```

4. **Test unsigned request (should get 403):**

   ```bash
   curl -vs http://localhost:8080/ -H 'Host: cdn.example.com'
   ```

5. **Sign a URL and test:**

   ```bash
   # Pick key3 from url_sig.config
   go run sign.go \
       --url http://cdn.example.com/ \
       --useparts 1 --algorithm 1 --duration 60 \
       --keyindex 3 --key <key3_value_from_config>
   ```

   Copy the output curl command, add `-H 'Host: cdn.example.com'` if hitting
   localhost, and run it. Should get a 200.

## Legacy Perl Scripts

The original `genkeys.pl` and `sign.pl` Perl scripts are still present for
backward compatibility. They require `Digest::SHA`, `Digest::HMAC_MD5`, and
`MIME::Base64::URLSafe`. The Go tools (`genkeys.go`, `sign.go`) are
functionally equivalent and have no dependencies beyond the Go standard
library.
