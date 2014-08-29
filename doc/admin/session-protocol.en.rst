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

.. _session-protocol:

Session Protocol
****************

Traffic Server supports some session level protocols in place or on
top of HTTP. These can be provided by a plugin
(:ref:`see <new-protocol-plugins>`) or be one that is supported
directly by Traffic Server. Currently the
`SPDY <http://www.chromium.org/spdy>`_ protocol is the only one current
supported but it is planned to support HTTP 2 when that is finalized.

Session protocols are specified by explicit names, based on the `NPN <https://technotes.googlecode.com/git/nextprotoneg.html>`_ names. The core supported names are

*  ``http/0.9``
*  ``http/1.0``
*  ``http/1.1``
*  ``http/2``
*  ``spdy/1``
*  ``spdy/2``
*  ``spdy/3``
*  ``spdy/3.1``

The ``http/2`` value is currently not functional but included for future use. ``spdy/1`` and ``spdy/2`` are obsolete but are include for completeness.

The session protocols supported on a proxy port are a subset of these values. For convenience some psuedo-values are defined in terms of these fundamental protocols.

*  ``http`` means ``http/0.9``, ``http/1.0``, and ``http/1.1``
*  ``spdy`` means ``spdy/3`` and ``spdy/3.1``.
*  ``http2`` means ``http/2``

Each proxy port can be
configured in :ts:cv:`proxy.config.http.server_ports` to support a subset of
these session protocols. For TLS enabled connections this
configuration controls which protocols are offered by NPN. For non-TLS
proxy ports protocol sniffing is used to determine which protocol is
being used by the client. If the detected protocol is not supported
for that proxy port the connection is dropped.
