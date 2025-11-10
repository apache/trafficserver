.. include:: ../../common.defs

.. _escalate-plugin:

Escalate Plugin
***************

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

The Escalate plugin allows Traffic Server to try an alternate
origin when the origin server in the remap rule is either unavailable
or returns specific HTTP error codes. Some services call this failover
or fail-action.

Plugin Configuration
--------------------

The escalate plugin is a remap plugin (not global) and takes a parameter
with two delimited fields: ``comma-separated-error-codes:secondary-origin-server``.  For instance,

    ``@pparam=401,404,410,502:second-origin.example.com``

would have Traffic Server send a cache miss to ``second-origin.example.com``
when the origin server in the remap rule returns a 401,
404, 410, or 502 error code.

@pparam=--pristine
  This option sends the "pristine" Host: header (eg, the Host: header
  that the client sent) to the escalated request.

@pparam=--no-redirect-header
  Controls whether to add the x-escalate-redirect header to escalated requests.
  When enabled (default), the plugin adds an x-escalate-redirect header with value "1"
  to the client request when it escalates to a different origin server. This header
  can be used by downstream systems to identify requests that have been escalated.
  The header is only added if it doesn't already exist in the request.
  Use --no-redirect-header to disable adding the x-escalate-redirect header.

@pparam=--escalate-non-get-methods
  In general, the escalate plugin is used with a failover origin that serves a
  cached backup of the original content.  As a result, the default behavior is
  to only escalate GET requests since POST, PUT, etc., are not idempotent and
  may require side effects that are not supported by a failover origin. This
  option overrides the default behavior and enables escalation for non-GET
  requests in addition to GET.

  .. note::

    For POST body buffering to work with escalation,
    :ts:cv:`proxy.config.http.post_copy_size` must be set large enough to buffer
    the expected POST body sizes (e.g., 2048 bytes or larger). This enables
    Traffic Server to buffer POST bodies before sending to the origin, so they
    can be replayed if escalation is needed.

Installation
------------

This plugin is considered stable and is included with |TS| by default. There
are no special steps necessary for its installation.

Example
-------

With this line in :file:`remap.config` ::

    map cdn.example.com origin.example.com \
      @plugin=escalate.so @pparam=401,404,410,502:second-origin.example.com @pparam=--pristine

Traffic Server would accept a request for ``cdn.example.com`` and, on a cache miss, proxy the
request to ``origin.example.com``. If the response code from that server is a 401, 404, 410,
or 502, then Traffic Server would proxy the request to ``second-origin.example.com``, using a
Host: header of ``cdn.example.com``. Additionally, an x-escalate-redirect header with value "1"
will be added to the escalated request.

To disable adding the x-escalate-redirect header, use::

    map cdn.example.com origin.example.com \
      @plugin=escalate.so @pparam=401,404,410,502:second-origin.example.com @pparam=--no-redirect-header

By default, only GET requests are escalated. To escalate non-GET requests as
well, you can use::

    map cdn.example.com origin.example.com \
      @plugin=escalate.so @pparam=401,404,410,502:second-origin.example.com @pparam=--escalate-non-get-methods

