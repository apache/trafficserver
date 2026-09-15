.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. default-domain:: cpp

TSHttpSsnClientReceivedErrorGet
===============================

Synopsis
--------

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSHttpSsnClientReceivedErrorGet(TSHttpSsn ssnp, uint32_t *error_class, uint64_t *error_code)
.. function:: void TSHttpSsnClientSentErrorGet(TSHttpSsn ssnp, uint32_t *error_class, uint64_t *error_code)

Description
-----------

These functions retrieve the most recent connection error received from or
sent to an HTTP/2 client. They can be used from a session hook, including
``TS_HTTP_SSN_CLOSE_HOOK``, even when the connection never created an HTTP
transaction. Both output pointers must be non-null. The caller must hold the
session mutex, as is the case in a session hook callback.

``error_class`` is zero when no connection error information is available and
one for a connection error. ``error_code`` is the HTTP/2 GOAWAY error code.
A code of zero denotes ``NO_ERROR`` and is a normal shutdown, even when the
class is one. For other client protocols both outputs are zero.

These are snapshots of the latest GOAWAY state, not cumulative counters.
They do not report stream errors. The corresponding transaction accessors
provide stream error information. A connection error can also be copied to
several transactions during teardown; consumers counting errors should count
it once at session close rather than once per affected transaction.

The session and returned error values may be used until the session-close
callback reenables the session with :func:`TSHttpSsnReenable`. The client
VConn may already be unavailable at that point. Plugins that need the client
address should preserve it from the session-start hook.
