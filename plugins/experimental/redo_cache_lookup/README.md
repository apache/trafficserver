# Redo Cache Lookup Plugin

This plugin shows how to use the experimental `TSHttpTxnRedoCacheLookup` api. It works by checking the cache for a fallback url if the cache lookup failed for any given url.

## Configuration

Add this plugin to `plugin.config` with the `--fallback` option:

```
redo_cache_lookup.so --fallback http://example.com/fallback_url
```