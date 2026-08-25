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

.. default-domain:: cpp

TSPortDescriptorParse
*********************

Parse and listen on a proxy port descriptor.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. type:: TSPortDescriptor
.. function:: TSPortDescriptor TSPortDescriptorParse(const char *descriptor)
.. function:: TSReturnCode TSPortDescriptorAccept(TSPortDescriptor descriptor, TSCont contp)
.. function:: void TSPortDescriptorDestroy(TSPortDescriptor descriptor)

Description
===========

:func:`TSPortDescriptorParse` parses the same descriptor syntax used by
:ts:cv:`proxy.config.http.server_ports` and returns an allocated, opaque
:type:`TSPortDescriptor` handle. Each successful call must be paired with
exactly one call to :func:`TSPortDescriptorDestroy`.

:func:`TSPortDescriptorAccept` copies the information it needs from
:arg:`descriptor` and does not retain a pointer to it. The descriptor can
therefore be destroyed immediately after :func:`TSPortDescriptorAccept`
returns, regardless of whether the listener remains active.

A descriptor containing only an ``fd=`` option is not supported because this
API requires an explicit listen endpoint. The ``quic`` option is also not
supported by this API and must not be used; it does not create a QUIC listener.

For example, this function destroys the descriptor after opening the listener:

.. code-block:: cpp

    TSReturnCode
    listen_on_descriptor(TSCont contp, const char *spec)
    {
      TSPortDescriptor descriptor = TSPortDescriptorParse(spec);
      if (descriptor == nullptr) {
        return TS_ERROR;
      }

      TSReturnCode result = TSPortDescriptorAccept(descriptor, contp);
      TSPortDescriptorDestroy(descriptor);
      return result;
    }

When a connection is accepted, :arg:`contp` receives
:enumerator:`TS_EVENT_NET_ACCEPT`. The event data is a :type:`TSVConn` for the
accepted connection.

Return Values
=============

:func:`TSPortDescriptorParse` returns a new descriptor handle when
:arg:`descriptor` was parsed successfully. It returns ``nullptr`` for a null
argument, invalid descriptor, or descriptor that cannot be used by
:func:`TSPortDescriptorAccept`.

:func:`TSPortDescriptorAccept` returns :enumerator:`TS_SUCCESS` when the
listener was opened. It returns :enumerator:`TS_ERROR` for a null argument, a
descriptor with an unusable listen endpoint, or an error opening the listener.

:func:`TSPortDescriptorDestroy` releases :arg:`descriptor`. Passing
``nullptr`` has no effect. Destroying a descriptor does not stop a listener
previously opened from it.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSNetAccept(3ts)`,
:manpage:`records.yaml(5)`
