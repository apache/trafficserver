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

.. class:: TSPortDescriptor
.. function:: TSReturnCode TSPortDescriptorParse(const char *descriptor, TSPortDescriptor *result)
.. function:: TSReturnCode TSPortDescriptorAccept(const TSPortDescriptor *descriptor, TSCont contp)

Description
===========

:func:`TSPortDescriptorParse` parses the same descriptor syntax used by
:ts:cv:`proxy.config.http.server_ports`. The parsed representation is written
to caller-owned :type:`TSPortDescriptor` storage. A descriptor must be
successfully parsed before it is passed to :func:`TSPortDescriptorAccept`.

The API does not allocate or free the :type:`TSPortDescriptor` object. No
separate destruction function is required. Its memory is released according
to its normal C++ storage duration: an automatic object is released when it
leaves scope, and a dynamically allocated object is released when the plugin
deletes it. Calling :func:`TSPortDescriptorParse` again reuses the same
descriptor storage.

:func:`TSPortDescriptorAccept` copies the information it needs from
:arg:`descriptor` and does not retain a pointer to it. The descriptor can
therefore leave scope immediately after :func:`TSPortDescriptorAccept`
returns, regardless of whether the listener remains active.

For example, the descriptor in this function is released when the function
returns while the listener continues to accept connections:

.. code-block:: cpp

    TSReturnCode
    listen_on_descriptor(TSCont contp, const char *spec)
    {
      TSPortDescriptor descriptor;

      if (TSPortDescriptorParse(spec, &descriptor) != TS_SUCCESS) {
        return TS_ERROR;
      }

      return TSPortDescriptorAccept(&descriptor, contp);
    }

When a connection is accepted, :arg:`contp` receives
:enumerator:`TS_EVENT_NET_ACCEPT`. The event data is a :type:`TSVConn` for the
accepted connection.

Return Values
=============

:func:`TSPortDescriptorParse` returns :enumerator:`TS_SUCCESS` when
:arg:`descriptor` was parsed successfully. It returns :enumerator:`TS_ERROR`
for a null argument or invalid descriptor. After a failed parse,
:arg:`result` remains invalid until it is parsed successfully.

:func:`TSPortDescriptorAccept` returns :enumerator:`TS_SUCCESS` when the
listener was opened. It returns :enumerator:`TS_ERROR` for a null argument, a
descriptor that was not successfully parsed, an unusable listen endpoint, or
an error opening the listener.

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSNetAccept(3ts)`,
:manpage:`records.yaml(5)`
