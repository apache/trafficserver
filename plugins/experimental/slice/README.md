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
of a remap line as follows (default 1MB slice in this example):

```
map http://ats-cache/ http://parent/ @plugin=slice.so @plugin=cache_range_requests.so
map https://ats-cache/ http://parent/ @plugin=slice.so @plugin=cache_range_requests.so
```

alternatively (2MB slice block)

```
map http://ats-cache/ http://parent/ @plugin=slice.so @pparam=-b @pparam=2M @plugin=cache_range_requests.so
map https://ats-cache/ http://parent/ @plugin=slice.so @pparam=--blockbytes=2M @plugin=cache_range_requests.so
```

Options for the slice plugin (typically last one wins):
```
--blockbytes=<number bytes> (optional)
  Slice block size.
  Default is 1m or 1048576 bytes.
  also -b <num bytes>
  Suffix k,m,g supported.
  Limited to 32k and 32m inclusive.
  For backwards compatibility blockbytes:<num bytes> is also supported.

--blockbytes-test=<number bytes> (optional)
  Slice block size for testing.
  also -t <num bytes>
  Suffix k,m,g supported.
  Limited to any positive number.
  Ignored if --blockbytes is provided.

--remap-host=<loopback hostname> (optional)
  Uses effective url with given host and port 0 for remapping.
  Requires setting up an intermediate loopback remap rule.
  -r for short

--pace-errorlog=<second(s)> (optional)
  Limit stitching error logs to every 'n' second(s)
  Default is to log all errors (no pacing).
  also -e <seconds>

--disable-errorlog (optional)
  Disable writing stitching errors to the error log.
  also -d
```

By default the plugin uses the pristine url to loopback call back
into the same rule as each range slice is issued.  The effective url
with loopback remap host may be used by adding the '-r <hostname>' or
'--remap-host=<hostname>' plugin option.

Using the `--remap-host` option splits the plugin chain into 2 remap rules.
One remap rule for all the incoming requests and the other for just the block
range requests.  This allows for easier trouble shooting via logs and
also allows for more effecient plugin rules.  The default pristine method
runs the remap plugins twice, one for the incoming request and one for
eace slice.  Splitting the rules allows for plugins like URI signing to
be done on the client request only.

NOTE: Requests NOT handled by the slice plugin (ie: HEAD requests) are
handled as with a typical remap rule.  GET requests intercepted by the
slice plugin are virtually reissued into ATS and are forward proxied
through the cache_range_requests plugin.

```
map http://ats/ http://parent/ @plugin=slice.so @pparam=--blockbytes=512k @pparam=--remap-host=loopback
map https://ats/ https://parent/ @plugin=slice.so @pparam=--blockbytes=512k @pparam=--remap-host=loopback

# Virtual forward proxy for slice range blocks
map http://loopback/ http://parent/ @plugin=cache_range_requests.so
map https://loopback/ http://parent/ @plugin=cache_range_requests.so
```

**Note**: For default pristine behavior cache_range_requests **MUST**
follow slice.so Put these plugins at the end of the plugin list

**Note**: blockbytes is defined in bytes. Postfix for 'K', 'M' and 'G'
may be used.  1048576 (1MB) is the default.

For testing purposes an unchecked value of "blockbytes-test" is also available.

Debug output can be enable by setting the debug tag: **slice**.  If debug
is enabled all block stitch errors will log to diags.log

The slice plugin is susceptible to block stitching errors caused by
mismatched blocks.  For these cases special detailed error logs are
provided to help with debugging.  Below is a sample error log entry::

```
[Apr 19 20:26:13.639] [ET_NET 17] ERROR: [slice] 1555705573.639 reason="Non 206 internal block response" uri="http://localhost:18080/%7Ep.tex/%7Es.50M/%7Eui.20000/" uas="curl/7.29.0" req_range="bytes=1000000-" norm_range="bytes 1000000-52428799/52428800" etag_exp="%221603934496%22" lm_exp="Fri, 19 Apr 2019 18:53:20 GMT" blk_range="21000000-21999999" status_got="400" cr_got="" etag_got="" lm_got="" cc="no-store" via=""
```

Current error types logged:
```
    Mismatch block Etag
    Mismatch block Last-Modified
    Non 206 internal block response
    Mismatch/Bad block Content-Range
```


With slice error logs disabled these type errors can typically be detected
by observing crc=ERR_READ_ERROR and pscl=0 in normal logs.

At the current time only single range requests or the first part of a
multi part range request of the forms:
```
Range: bytes=<begin>-<end>
Range: bytes=<begin>-
Range: bytes=-<last N bytes>
```
are supported as multi part range responses are non trivial to implement.
This matches with the cache_range_requests.so plugin capability.
---

Important things to note:

Any first block server response that is not a 206 is passed down to
the client.

Only the first server response block is used to evaluate any "If-"
headers.  Subsequent server slice block requests remove these headers.

If a client aborts mid transaction the current slice block is completed
to ensure that the block is written to cache.

The only 416 case this plugin handles itself is if the requested range
is inside the end slice block but past the content length.  Otherwise
parents seem to properly issue 416 responses themselves.

---

To manually build the plugin use the "tsxs" executable that installs with
traffic_server.

Running the following command will build the plugin

```
tsxs -v -o slice.so *.cc
```

Running the following command will build and install the plugin.
Beware this may crash a running system if the plugin is loaded
and the OS uses memory paging with plugins.

```
tsxs -v -i -o slice.so *.cc
```
