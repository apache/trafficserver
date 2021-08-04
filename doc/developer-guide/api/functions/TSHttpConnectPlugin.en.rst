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

.. include:: ../../../common.defs

.. default-domain:: c

TSHttpConnectPlugin
*************************

Allows the plugin to initiate an http connection. This will tag the
HTTP state machine with extra data that can be accessed by the
logging interface. Additional arguments provide buffer settings that
are used when constructing IOBuffers. The connection is treated as
an HTTP transaction as if it came from a client.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSVConn TSHttpConnectPlugin(TSHttpConnectOptions * options);

Description
===========

This call attempts to create an HTTP state machine and a virtual
connection to that state machine. This is more efficient than using
:c:func:`TSNetConnect` because it avoids using the operating system
stack via the loopback interface.

:arg:`options`
   A :c:type:`TSHttpConnectOptions` structure that contains fields
   that provide the network address of the target, a tag that can be
   passed through to the HTTP state machine, a plugin ID, a buffer
   index and buffer water mark.

The virtual connection returned as the :c:type:`TSVConn` is API
equivalent to a network virtual connection both to the plugin and
to internal mechanisms. Data is read and written to the connection
(and thence to the target system) by reading and writing on this
virtual connection.

.. note::

   This function only opens the connection. To drive the transaction an actual
   HTTP request must be sent and the HTTP response handled. The transaction is
   handled as a standard HTTP transaction and all of the standard configuration
   options and plugins will operate on it.

The combination of :arg:`tag` and :arg:`id` is intended to enable correlation
in log post processing. The :arg:`tag` identifies the connection as related
to the plugin and the :arg:`id` can be used in conjunction with plugin
generated logs to correlate the log records.

Notes
=====

The H2 implementation uses this to correlate client sessions
with H2 streams. Each client connection is assigned a distinct
numeric identifier. This is passed in the options structure via
the member variable `id` to the :c:func:`TSHttpConnectPlugin` function.
The :arg:`tag` is selected to be the NPN string for the client session
protocol, e.g. "h2". Log post processing can then count the number of
connections for the various supported protocols and the number of H2
virtual streams for each real client connection to Traffic Server.

See Also
========

:manpage:`TSHttpConnectWithPluginId(3ts)`,
:manpage:`TSHttpConnect(3ts)`,
:manpage:`TSNetConnect(3ts)`,
:manpage:`TSAPI(3ts)`
