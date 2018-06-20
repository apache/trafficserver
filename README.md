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
  tsxs -v -i -o slice.so ContentRange.cc HttpHeader.cc intercept.cc range.cc slice.cc
  compiling ContentRange.cc -> ContentRange.lo
  compiling HttpHeader.cc -> HttpHeader.lo
  compiling intercept.cc -> intercept.lo
  compiling range.cc -> range.lo
  compiling slice.cc -> slice.lo
  linking -> slice.so
  installing slice.so -> .....
```  

Debug output can be enable by setting the debug tag: **slice**

Example output of a range request:

```
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

Basic shared data:
```

struct Channel
{
  TSVIO m_vio { nullptr };
  TSIOBuffer m_iobuf { nullptr };
  TSIOBufferReader m_reader { nullptr };
};

struct Stage // upstream or downstream (server or client)
{
  TSVConn m_vc { nullptr };
  Channel m_read;
  Channel m_write;

  HdrMgr m_hdr_mgr; // header memory manager
};

struct Data
{
  int64_t m_blocksize;
  sockaddr_storage m_client_ip;

  TSHttpStatus m_statustype; // 200 or 206

  bool m_bail; // non 206/200 response

  std::pair<int64_t, int64_t> m_range_begend;
  int64_t m_contentlen;

  int64_t m_blocknum; //!< block number to work on, -1 bad/stop
  int64_t m_skipbytes; //!< number of bytes to skip in this block

  int64_t m_bytestosend; //!< header + content bytes to send
  int64_t m_bytessent; //!< number of content bytes sent

  bool m_server_block_header_parsed;
  bool m_server_first_header_parsed;
  bool m_client_header_sent;

  Stage m_upstream;
  Stage m_dnstream;

  TSHttpParser m_http_parser; //!< cached for reuse
};

```
