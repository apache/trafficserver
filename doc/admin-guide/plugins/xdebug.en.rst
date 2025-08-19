.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. default-domain:: cpp

.. _admin-plugins-xdebug:

XDebug Plugin
*************

The `XDebug` plugin allows HTTP clients to debug the operation of
the Traffic Server cache using the default ``X-Debug`` header. The plugin
is triggered by the presence of the ``X-Debug`` or the configured header in
the client request. The contents of this header should be the names of the
debug headers that are desired in the response. The `XDebug` plugin
will inject the desired headers into the client response.  In addition, a value of the
form ``fwd=n`` may appear in the ``X-Debug`` header, where ``n`` is a nonnegative
number.  If ``n`` is zero, the ``X-Debug`` header will be deleted.  Otherwise, ``n`` is
decremented by 1.  ``=n`` may be omitted, in which case the ``X-Debug`` header will
not be modified or deleted.

`XDebug` is a global plugin. It is installed by adding it to the
:file:`plugin.config` file. It currently takes a single, optional
configuration option, ``--header``. E.g.

::

    --header=ATS-My-Debug

This overrides the default ``X-Debug`` header name.

All the debug headers are disabled by default, and you need to enable them
selectively by passing header names to ``--enable`` option.

::

    --enable=x-remap,x-cache

This enables ``X-Remap`` and ``X-Cache``. If a client's request has
``X-Debug: x-remap, x-cache, probe``, XDebug will only inject ``X-Remap`` and
``X-Cache``.

To enable the JSON transaction header probe functionality:

::

    --enable=probe-full-json

This allows clients to request ``X-Debug: probe-full-json`` to receive request
and response header information in a structured JSON format.


Debugging Headers
=================

The `XDebug` plugin is able to generate the following debugging headers:

Via
    If the ``Via`` header is requested, the `XDebug` plugin sets the
    :ts:cv:`proxy.config.http.insert_response_via_str` configuration variable
    to ``3`` for the request.

Diags
    If the ``Diags`` header is requested, the `XDebug` plugin enables the
    transaction specific diagnostics for the transaction. This also requires
    that :ts:cv:`proxy.config.diags.debug.enabled` is set to ``1``.

Probe
    All request and response headers are written to the response body. Because
    the body is altered, it disables writing to cache.
    In conjunction with the `fwd` tag, the response body will contain a
    chronological log of all headers for all transactions used for this
    response.

    Layout:

    - Request Headers from Client  -> Proxy A
    - Request Headers from Proxy A -> Proxy B
    - Original content body
    - Response Headers from Proxy B -> Proxy A
    - Response Headers from Proxy A -> Client

Probe-Full-JSON
    Similar to `Probe` but formats the output as a complete JSON object
    containing request and response headers. The response body is modified to
    include client request headers, proxy request headers, the original server
    response body, server response headers, and proxy response headers in a
    structured JSON format. In contrast to Probe, the response content with
    this feature is parsable with JSON parsing tools like ``jq``. Because the
    body is altered, it disables writing to cache and changes the Content-Type
    to ``application/json``.

    JSON Nodes:

    - ``client-request``: Headers from the client to the proxy.
    - ``proxy-request``: Headers from the proxy to the origin server (if applicable).
    - ``server-body``: The original response body content from the origin server.
    - ``server-response``: Headers from the origin server to the proxy.
    - ``proxy-response``: Headers from the proxy to the client.

    Here's an example of the JSON output from the `x_probe_full_json` test::

        $ curl -s -H"uuid: 1" -H "Host: example.com" -H "X-Debug: probe-full-json" http://127.0.0.1:61003/test | jq
        {
            "client-request": {
                "start-line": "GET http://127.0.0.1:61000/test HTTP/1.1",
                "uuid": "1",
                "host": "127.0.0.1:61000",
                "x-request": "from-client"
            },
            "proxy-request": {
                "start-line": "GET /test HTTP/1.1",
                "uuid": "1",
                "host": "127.0.0.1:61000",
                "x-request": "from-client",
                "Client-ip": "127.0.0.1",
                "X-Forwarded-For": "127.0.0.1",
                "Via": "http/1.1 traffic_server[f47ffc16-0a20-441e-b17d-6e3cb044e025] (ApacheTrafficServer/10.2.0)"
            },
            "server-body": "Original server response",
            "server-response": {
                "start-line": "HTTP/1.1 200 ",
                "content-type": "text/html",
                "content-length": "24",
                "x-response": "from-origin",
                "Date": "Tue, 19 Aug 2025 15:02:07 GMT"
            },
            "proxy-response": {
                "start-line": "HTTP/1.1 200 OK",
                "content-type": "application/json",
                "x-response": "from-origin",
                "Date": "Tue, 19 Aug 2025 15:02:07 GMT",
                "Age": "0",
                "Transfer-Encoding": "chunked",
                "Connection": "keep-alive",
                "Server": "ATS/10.2.0",
                "X-Original-Content-Type": "text/html"
            }
        }

X-Cache-Key
    The ``X-Cache-Key`` header contains the URL that identifies the HTTP object in the
    Traffic Server cache. This header is particularly useful if a custom cache
    key is being used.

X-Cache
    The ``X-Cache`` header contains the results of any cache lookups.

    ==========  ===========
    Value       Description
    ==========  ===========
    none        No cache lookup was attempted.
    miss        The object was not found in the cache.
    hit-stale   The object was found in the cache, but it was stale.
    hit-fresh   The object was fresh in the cache.
    skipped     The cache lookup was skipped.
    ==========  ===========

    If a request goes through multiple proxies, each one appends its X-Cache header content
    the end of the existing X-Cache header. This is the same order as for the
    ``Via`` header.

X-Cache-Generation
  The cache generation ID for this transaction, as specified by the
  :ts:cv:`proxy.config.http.cache.generation` configuration variable.

X-Milestones
    The ``X-Milestones`` header contains detailed information about
    how long the transaction took to traverse portions of the HTTP
    state machine. The timing information is obtained from the
    :func:`TSHttpTxnMilestoneGet` API. Each milestone value is a
    fractional number of seconds since the beginning of the
    transaction.

X-Transaction-ID
    A unique transaction ID, which identifies this request / transaction. This
    matches the log field format that is also available, %<cruuid>.

X-Remap
    If the URL was remapped for a request, this header gives the *to* and *from* field from the line in remap.config that caused
    the URL to be remapped.

X-Effective-URL
    If the URL was remapped for a request, this header gives the URL resulting from the remapping. Note that if there are
    multiple remaps, this header aggregates the URLs, space-comma-separated. The URLs are inside doublequotes.

X-ParentSelection-Key
    The ``X-ParentSelection-Key`` header contains the URL that is used to
    determine parent selection for an object in the Traffic Server. This
    header is particularly useful if a custom parent selection key is
    being used.
