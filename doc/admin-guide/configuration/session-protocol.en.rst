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

.. _session-protocol:

Session Protocol
****************

|TS| supports some session level protocols in place of or on top of HTTP. These
can be provided by a plugin (see :ref:`developer-plugins-new-protocol-plugins`)
or be one that is supported directly by |TS|. Note that the SPDY protocol is
deprecated as of v6.2.0, and will be removed in v7.0.0.

Session protocols are specified by explicit names:

*  ``http/0.9``
*  ``http/1.0``
*  ``http/1.1``
*  ``http/2``
*  ``spdy/1``
*  ``spdy/2``
*  ``spdy/3``
*  ``spdy/3.1``

The session protocols supported on a proxy port are a subset of these values.
For convenience some pseudo-values are defined in terms of these fundamental
protocols:

*  ``http`` means ``http/0.9``, ``http/1.0``, and ``http/1.1``
*  ``spdy`` means ``spdy/3`` and ``spdy/3.1``.
*  ``http2`` means ``http/2``

Each proxy port can be configured in :ts:cv:`proxy.config.http.server_ports`
to support a subset of these session protocols. For TLS enabled connections this
configuration controls which protocols are offered by NPN. Protocol sniffing is
use for non-TLS proxy ports to determine which protocol is being used by the
client. If the detected protocol is not supported for that proxy port the
connection is dropped.
