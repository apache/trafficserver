### Apache Traffic Server - Slicer Plugin

The purpose of this plugin is to slice full file or range based requests
into deterministic chunks.

Deterministic chunks are requested from a parent cache or origin server
using a preconfigured block size.

To enable the plugin, specify the plugin library via @plugin at the end
of a remap line as follows:

map http://ats-server/somepath/ http://originserver/somepath @plugin=slicer.so @pparam=blocksize=1m @plugin=cache_range_requests.so

**Note**: blocksize is defined in megabytes. 1MB is the default.

__To build the plugin__, just set your path to include the tsxs binary in your path and run make.

```
  $ make
  tsxs -i -o slicer.so slicer.cc cache.cc config.cc ipaddress.cc mime.cc range.cc request.cc response.cc transaction.cc transform.cc util.cc
  compiling slicer.cc -> slicer.lo
  compiling cache.cc -> cache.lo
  compiling config.cc -> config.lo
  compiling ipaddress.cc -> ipaddress.lo
  compiling mime.cc -> mime.lo
  compiling range.cc -> range.lo
  compiling request.cc -> request.lo
  compiling response.cc -> response.lo
  compiling transaction.cc -> transaction.lo
  compiling transform.cc -> transform.lo
  compiling util.cc -> util.lo
  linking -> slicer.so
  installing slicer.so -> /opt/trafficserver/libexec/trafficserver/slicer.so
```  

Debug output can be enable by setting the debug tag: **slicer**

Example output of a range request:

```
[range.cc:00070] calculate(): ------------------------------------------
[range.cc:00071] calculate(): (range)  total file_size: 283449442
[range.cc:00072] calculate(): (range)   content_length: 283449442
[range.cc:00073] calculate(): (range)       block_size: 2097152
[range.cc:00074] calculate(): (range)       num_blocks: 136
[range.cc:00075] calculate(): (range)      block_bytes: 285212672
[range.cc:00076] calculate(): (range)     excess_bytes: 1763230
[range.cc:00077] calculate(): ------------------------------------------
[range.cc:00078] calculate(): .            start_range: 372389
[range.cc:00080] calculate(): .         startblock_num: 0 (offset 0)
[range.cc:00081] calculate(): .       startblock_bytes: 1724763
[range.cc:00082] calculate(): .        startblock_skip: 372389
[range.cc:00083] calculate(): .              end_range: 283821831
[range.cc:00085] calculate(): .           endblock_num: 135 (offset 283115520)
[range.cc:00086] calculate(): .         endblock_bytes: 706311
[range.cc:00087] calculate(): .        endblock_remain: 1390841
[range.cc:00088] calculate(): ------------------------------------------
[transaction.cc:00648] handleSendResponseHeader(): Client Response Code: 200
```

**Note:** This plugin is still currently under heavy development. Nothing here probably works yet.

Debug messages related to object instance construction/deconstruction, see slicer.h.  

At the current time only single range requests or the first part of a 
multi part range request of the forms:
```
Range: bytes=<begin>-<end>
Range: bytes=<begin>-
```
are supported as multi part range responses are non trivial to implement.
This matches with the cache_range_requests.so plugin.
