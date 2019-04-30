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

    --header=ATS-My-Debug

This overrides the default ``X-Debug`` header name.


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

X-Cache-Key
    The ``X-Cache-Key`` header contains the URL that identifies the HTTP object in the
    Traffic Server cache. This header is particularly useful if a custom cache
    key is being used.

X-Cache
    The ``X-Cache`` header contains the results of any cache lookup.

    ==========  ===========
    Value       Description
    ==========  ===========
    none        No cache lookup was attempted.
    miss        The object was not found in the cache.
    hit-stale   The object was found in the cache, but it was stale.
    hit-fresh   The object was fresh in the cache.
    skipped     The cache lookup was skipped.
    ==========  ===========

X-Cache-Generation
  The cache generation ID for this transaction, as specified by the
  :ts:cv:`proxy.config.http.cache.generation` configuration variable.

X-Milestones
    The ``X-Milestones`` header contains detailed information about
    how long the transaction took to traverse portions of the HTTP
    state machine. The timing information is obtained from the
    :c:func:`TSHttpTxnMilestoneGet` API. Each milestone value is a
    fractional number of seconds since the beginning of the
    transaction.

X-Transaction-ID
    A unique transaction ID, which identifies this request / transaction. This
    matches the log field format that is also available, %<cruuid>.

X-Remap
    If the URL was remapped for a request, this header gives the *to* and *from* field from the line in remap.config that caused
    the URL to be remapped.
