.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

TSUuidCreate
************

Traffic Server UUID construction APIs.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSUuid TSUuidCreate(void)
.. function:: TSReturnCode TSUuidInitialize(TSUuid uuid, TSUuidVersion v)
.. function:: void TSUuidDestroy(TSUuid uuid)
.. function:: TSReturnCode TSUuidCopy(TSUuid dest, const TSUuid src)
.. function:: const char * TSUuidStringGet(const TSUuid uuid)
.. function:: TSUuidVersion TSUuidVersionGet(const TSUuid uuid)
.. function:: TSReturnCode TSUuidStringParse(TSUuid uuid, const char * uuid_str)
.. function:: const TSUuid TSProcessUuidGet(void)
.. function:: TSReturnCode TSClientRequestUuidGet(TSHttpTxn txnp, char* uuid_str)

Description
===========

These APIs are used to create, manage, and use UUIDs in a plugin, implementing
part of RFC 4122. Currently, only the V4 variant of the specifications is
implemented. In addition, an internal, process unique UUID is provided, which
can be used to uniquely identifying the running Traffic Server process.

:func:`TSUuidCreate` creates a new :type:`TSUuid` object, which is returned
and can be used by the other APIs. Similarly, a read-only global process UUID
is returned from the function :func:`TSProcessUuidGet`. You must not attempt
to modify any data as returned by either of these functions.

:func:`TSUuidInitialize` initializes a :type:`TSUuid` object, using the
algorithm defined for the specified version. Note that only the V4 variants is
currently supported. You can call :func:`TSUuidInitialize` repeatedly, which
each generates a new UUID, but this will overwrite any existing UUID data in
the object. This also implies that any strings retrieved using
:func:`TSUuidStringGet` are also modified accordingly.

:func:`TSUuidDestroy` destroys (releases) an :type:`TSUuid` object, and frees
all memory associated with this object. This includes any strings as returned
by e.g. :func:`TSUuidStringGet`.

:func:`TSUuidCopy` copies one :type:`TSUuid` to another, making an exact
duplicate. Note that both the source and the destination UUIDs must be created
appropriately, and should not have been previously destroyed.

:func:`TSUuidVersionGet` returns the version number for the
:type:`TSUuid`. This will work properly for any RFC 4122 initialized UUID
object, e.g. if you parse a string with :func:`TSUuidStringParse` this will
return the correct variant ID.

:func:`TSUuidStringGet` returns a pointer to the internal string
representation of the :type:`TSUuid` object. It's important to know that there
is no transfer of ownership of this string. If you need a copy of it, you are
responsible of doing so yourself. In particular, using a string as returned by
:func:`TSUuidStringGet` **after** you have called :func:`TSUuidDestroy` on the
corresponding :type:`TSUuid` object is a serious error. The UUID object does
not do any sort of reference counting on the string, and you must absolutely
not free the memory as returned by this API.

:func:`TSUuidStringParse` can be used to convert an existing
:type:`TSUuid` string to a Traffic Server UUID object. This will only succeed
if the :type:`TSUuid` string is a proper *RFC 4122* UUID. The :type:`TSUuid`
argument passed to this function must be a properly :func:`TSUuidCreate`
object, but it does not need to be previously initialized.

Finally, :func:`TSClientRequestUuidGet` can be used to extract
the client request uuid from a transaction. The output buffer must be of
sufficient length, minimum of ``TS_CRUUID_STRING_LEN`` + 1 bytes. This
produces the same string as the log tag %<cruuid> generates, and it will
be NULL terminated.

Return Values
=============

The :type:`TSUuid` type is an opaque pointer to an internal representation of
the UUID object. Several of the functions returns a normal Traffic Server
return status code, :type:`TSReturnCode`. You should verify the success of
those APIs, of course.

The :func:`TSUuidStringGet` function will return ``NULL`` if the :type:`TSUuid`
object is not properly initialized. Likewise, :func:`TSUuidVersionGet` would
then return ``TS_UUID_UNDEFINED``.

The :func:`TSUuidDestroy` function can not fail, and does not have a return
value,  but you are of course responsible for providing a valid :type:`TSUuid`
object.

Examples
========

.. code-block:: c

    #include <ts/ts.h>

    TSUuid machine, uuid;

    machine = TSProcessUuidGet();
    printf("Machine UUID is %s\n", TSUuidStringGet(machine);

    if (uuid = TSUuidCreate()) {
      if (TS_SUCCESS == TSUuidInitialize(uuid, TS_UUID_V4) {
        printf("My UUID is %s\n", TSUuidStringGet(uuid));
      }
      TSUuidDestroy(uuid);
    }

    const char* str = "c71e2bab-90dc-4770-9535-c9304c3de38e";

    if (TS_SUCCESS == TSUuidStringParse(uuid, str)) {
      if (TS_UUID_V4 == TSUuidVersionGet(uuid)) {
        // Yes!
      }
    }

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSUuid(3ts)`,
:manpage:`TSReturnCode(3ts)`,
