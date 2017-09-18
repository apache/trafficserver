# Description
This plugin allows some common cache key manipulations based on various HTTP request elements.  It can

* sort query parameters to prevent query parameters reordereding from being a cache miss
* ignore specific query parameters from the cache key by name or regular expression
* ignore all query parameters from the cache key
* only use specific query parameters in the cache key by name or regular expression
* include headers or cookies by name
* capture values from the `User-Agent` header.
* classify request using `User-Agent` and a list of regular expressions

# Documentation
Details and examples can be found in [cachekey plugin documentation](../../../doc/admin-guide/plugins/cachekey.en.rst).
