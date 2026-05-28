# Redo Cache Lookup Example Plugin

This plugin shows how to use the `TSHttpTxnRedoCacheLookup` C API. It
checks cache lookup results and asks ATS to retry the lookup with a fallback
URL when the original lookup misses or is skipped.

## Configuration

Add this plugin to `plugin.config` with the `--fallback` option:

```
redo_cache_lookup.so --fallback http://example.com/fallback_url
```
