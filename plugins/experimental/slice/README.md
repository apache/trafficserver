### Apache Traffic Server - Slicer Plugin

The purpose of this plugin is to slice full file or range based requests
into deterministic chunks.  This allows a large file to be spread across
multiple cache stripes and allows range requests to be satisfied by
stitching these chunks together.

Deterministic chunks are requested from a parent cache or origin server
using a preconfigured block byte size.

The plugin is an example of an intercept handler which takes a single
incoming request (range or whole asset), breaks it into a sequence
of block requests and assembles those blocks into a client response.
The plugin uses TSHttpConnect to delegate each block request to
cache_range_requests.so which handles all cache and parent interaction.

To enable the plugin, specify the plugin library via @plugin at the end
of a remap line as follows (2MB slice in this example):

```
map http://ats-cache-server/ http://parent/ @plugin=slice.so @pparam=blockbytes:2097152 @plugin=cache_range_requests.so
```

for global plugins.

```
slice.so blockbytes:2097152
cache_range_requests.so
```

**Note**: cache_range_requests **MUST** follow slice.so Put these plugins at the end of the plugin list
**Note**: blockbytes is defined in bytes. 1048576 (1MB) is the default.

For testing purposes an unchecked value of "blockbytestest" is also available.

Debug output can be enable by setting the debug tag: **slice**

Debug messages related to object instance construction/deconstruction, see slice.h.  

At the current time only single range requests or the first part of a 
multi part range request of the forms:
```
Range: bytes=<begin>-<end>
Range: bytes=<begin>-
Range: bytes=-<last N bytes>
```
are supported as multi part range responses are non trivial to implement.
This matches with the cache_range_requests.so plugin.
capability.
