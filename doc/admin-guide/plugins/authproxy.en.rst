.. _admin-plugins-authproxy:

AuthProxy Plugin
****************

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

There are many ways of authorizing an HTTP request. Often, this
requires making IPC calls to some internal infrastructure. ``AuthProxy``
is a plugin that takes care of the Traffic Server end of authorizing
a request and delegates the authorization decision to an external
HTTP service.

This plugin can be used as either a global plugin or a remap plugin.

Note that Traffic Server optimizes latency by skipping the DNS
lookup state if a document is found in the cache. This will have
the effect of serving the document without consulting the ``AuthProxy``
plugin. you can disable this behavior by setting
:ts:cv:`proxy.config.http.doc_in_cache_skip_dns` to ``0`` in
:file:`records.config`.

Note that the authorization request will need to match a remap rule
(which, as a standalone remap rule, does not need to call the
AuthProxy plugin). If a second remap rule is required, by default,
the authorization request will not have the same Host header as
the request from the client. It could be added using the
``header_rewrite`` plugin (set-header Host "pristine_host.example.com").

Plugin Options
--------------

--auth-transform=TYPE
  This option specifies how to route the incoming request to the
  authorization service. The transform type may be ``head`` or
  ``redirect``.

  If the transform type is ``head``, then the incoming request is
  transformed to a HEAD request and is sent to the same destination.
  If the response is ``200 OK``, the incoming request is allowed
  to proceed.

  If the transform type is ``range``, then the incoming request is
  transformed to a Range request asking for 0 bytes. Other than that,
  the behavior is identical to the ``head`` option above. This type
  of Range request is useful when the upstream destination is a cache,
  and it's not able to cache HEAD requests.

  If the transform type is ``redirect`` then the incoming
  request is sent to the authorization service designated by the
  `--auth-host` and `--auth-port` parameters. If the response is
  200 OK, the incoming request is allowed to proceed.

  When the authorization service responds with a status other than
  200 OK, that response is returned to the client as the response to
  the incoming request. This allows mechanisms such as HTTP basic
  authentication to work correctly. Note that the body of the
  authorization service response is not returned to the client.

--auth-host=HOST
  The name or address of the authorization host. This is only used
  by the ``redirect`` transform.

--auth-port=PORT
  The TCP port of the authorization host. This is only used by the
  ``redirect`` transform.

--force-cacheability
  If this options is set, the plugin will allow Traffic Server to
  cache the result of authorized requests. In the normal case, requests
  with authorization headers are nor cacheable, but this flag allows
  that by setting the :ts:cv:`proxy.config.http.cache.ignore_authentication`
  option on the request.

--cache-internal
  The option will allow the Traffic Server to cache internal
  requests. By default, internally generated requests are
  not cached as the agent needs to take the authorization decisions.

Examples
--------

In this example, the authentication is performed by converting the incoming
HTTP request to a `HEAD` request and sending that to the origin server
`origin.internal.com`::

  map http://cache.example.com http://origin.internal.com/ \
    @plugin=authproxy.so @pparam=--auth-transform=head

  map http://origin.internal.com http://origin.internal.com/


In this example, the request is directed to a local authentication server
that authorizes the request based on internal policy rules::

  map http://cache.example.com http://origin.internal.com/ \
    @plugin=authproxy.so @pparam=--auth-transform=redirect @pparam=--auth-host=127.0.0.1 @pparam=--auth-port=9000

  map http://origin.internal.com/ http://origin.internal.com/ \
    @plugin=authproxy.so @pparam=--auth-transform=redirect @pparam=--auth-host=127.0.0.1 @pparam=--auth-port=9000
