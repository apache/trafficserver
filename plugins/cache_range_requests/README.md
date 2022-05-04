
Thousands of range requests for a very large object in the traffic server
cache are likely to increase system load averages due to I/O wait as
objects are stored on a single stripe or disk drive.

This plugin allows you to remap individual range requests so that they
are stored as individual objects in the ATS cache when subsequent range
requests are likely to use the same range.  This spreads range requests
over multiple stripes thereby reducing I/O wait and system load averages.

This plugin reads the range request header byte range value and then
creates a new cache key url using the original request url with the range
value appended to it.  The range header is removed where appropriate
from the requests and the origin server response code is changed from
a 206 to a 200 to insure that the object is written to cache using the
new cache key url.  The response code sent to the client will be changed
back to a 206 and all requests to the origin server will contain the
range header so that the correct response is received.

Configuration:

    Add @plugin=cache_range_requests.so to your remap.config rules.

    Or for a global plugin where all range requests are processed,
    Add cache_range_requests.so to the plugin.config

Parent Selection Mode (consistent-hash only):

    default: Parent selection is based solely on the hash of a URL Path
             In this mode, all partial content of a URL is requested
             from the same upstream parent cache listed in parent.config

    Cache_key_url: Parent selection is based on the full cache_key_url
                   which includes information about the partial content
                   range.  In this mode, all requests (include partial
                   content) will use consistent hashing method for
                   parent selection.

    To enable cache_key_url parent select mode, the following param must be set:

    Global Plugin (plugin.config):

      cache_range_requests.so -p
      cache_range_requests.so --ps-cachekey

    Remap Plugin (remap.config):

      <from-url> <to-url> @plugin=cache_range_requests.so @pparam=--ps-cachekey
      <from-url> <to-url> @plugin=cache_range_requests.so @pparam=-p

X-CRR-IMS header support

    To support slice plugin self healing an option to force
    revalidation after cache lookup complete was added.  This option
		is triggered by a special header:

    This optional header looks like:

    X-CRR-IMS: Tue, 19 Nov 2019 13:26:45 GMT

		If the cache lookup was a cache hit and the cache header date
		is *less* than this header value then the cache state is switched
		from FRESH to STALE which results in If-Modified-Since or
		If-Match request being passed to the parent.

		In order for this option to be enabled the the following parameter
		must be set:

    Global Plugin (plugin.config):

      cache_range_requests.so --consider-ims
      cache_range_requests.so -c

    Remap Plugin (remap.config):

      <from-url> <to-url> @plugin=cache_range_requests.so @pparam=--consider-ims
      <from-url> <to-url> @plugin=cache_range_requests.so @pparam=-c

    Consider using the header_rewrite plugin to protect the parent
		from using this option as an attack vector against an origin.

Object Cacheability

    Normally objects are forced into the cache by changing the status code in the
    response from the upstream host from 206 to 200. The default behavior is to
    perform this operation blindly without checking cacheability. Add the `-v`
    flag to cause the plugin to ensure the object is cacheable; when it is not,
    the 206 status code is restored and the object will not be cached.

    Global Plugin (plugin.config):

      cache_range_requests.so --verify-cacheability
      cache_range_requests.so -v

    Remap Plugin (remap.config):

      <from-url> <to-url> @plugin=cache_range_requests.so @pparam=--verify-cacheability
      <from-url> <to-url> @plugin=cache_range_requests.so @pparam=-v

Caching Complete Responses

    To enable caching of complete responses, that is, a 200 OK instead of a 206 Partial
    Content response, add the `-r` flag to the plugin parameters. By default, complete
    responses are marked as uncacheable.

    Global Plugin (plugin.config):

      cache_range_requests.so --cache-complete-responses
      cache_range_requests.so -r

    Remap Plugin (remap.config):

      <from-url> <to-url> @plugin=cache_range_requests.so @pparam=--cache-complete-responses
      <from-url> <to-url> @plugin=cache_range_requests.so @pparam=-r