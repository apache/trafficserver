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
Host: header of ``cdn.example.com``.
