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

.. include:: ../../../common.defs

.. _developer-plugins-http-headers-system:

Traffic Server HTTP Header System
*********************************

.. toctree::
   :maxdepth: 2

No Null-Terminated Strings
==========================

It's not safe to assume that string data contained in marshal buffers
(such as URLs and MIME fields) is stored in null-terminated string
copies. Therefore, your plugins should always use the length parameter
when retrieving or manipulating these strings. You **cannot** pass in
``NULL`` for string-length return values; string values returned from
marshall buffers are not null-terminated. If you need a null-terminated
value, then use ``TSstrndup`` to automatically null-terminate a string.
The strings that come back and are not null-terminated **cannot** be
passed into the common ``str*()`` routines

.. note::
   Values returned from a marshall buffer can be ``NULL``, which means the
   field or object requested does not exist.

For example (from the ``blacklist_1`` sample)

.. code-block:: c

   char *host_string;
   int host_length;
   host_string = TSUrlHostGet (bufp, url_loc, &host_length);
   for (i = 0; i < nsites; i++) {
   if (strncmp (host_string, sites[i], host_length) == 0) {
      // ...
   }

See the sample plugins for additional examples.

Duplicate MIME Fields Are Not Coalesced
=======================================

MIME headers can contain more than one MIME field with the same name.
Earlier versions of Traffic Server joined multiple fields with the same
name into one field with composite values. This behavior came at a
performance cost and caused interoperability problems with older clients
and servers. Therefore, this version of Traffic Server does not coalesce
duplicate fields.

Properly-behaving plugins should check for the presence of duplicate
fields and then iterate over the duplicate fields via
:c:func:`TSMimeHdrFieldNextDup`.

MIME Fields Always Belong to an Associated MIME Header
======================================================

When using Traffic Server, you cannot create a new MIME field without an
associated MIME header or HTTP header; MIME fields are always seen as
part of a MIME header or HTTP header.

To use a MIME field, you must specify the MIME header or HTTP header to
which it belongs - this is called the field's **parent header**. The
``TSMimeField*`` functions in older versions of the SDK have been
deprecated, as they do not require the parent header as inputs. The
current version of Traffic Server uses new functions, the
**``TSMimeHdrField``** series, which require you to specify the location
of the parent header along with the location of the MIME field. For
every deprecated *``TSMimeField``* function, there is a new, preferred
``TSMimeHdrField*`` function. Therefore, you should use the
**``TSMimeHdrField``** functions instead of the deprecated
*``TSMimeField``* series. Examples are provided below.

Instead of:

.. code-block:: c

    TSMLoc TSMimeFieldCreate (TSMBuffer bufp)

You should use:

.. code-block:: c

    TSMLoc TSMimeHdrFieldCreate (TSMBuffer bufp, TSMLoc hdr)

Instead of:

.. code-block:: c

    void TSMimeFieldCopyValues (TSMBuffer dest_bufp, TSMLoc dest_offset,
       TSMBuffer src_bufp, TSMLoc src_offset)

You should use:

.. code-block:: c

    void TSMimeHdrFieldCopyValues (TSMBuffer dest_bufp, TSMLoc dest_hdr,
       TSMLoc dest_field, TSMBuffer src_bufp, TSMLoc src_hdr, TSMLoc
       src_field)

In the ``TSMimeHdrField*`` function prototypes, the ``TSMLoc`` field
corresponds to the ``TSMLoc`` offset used the deprecated
``TSMimeField*`` functions (see the discussion of parent ``TSMLoc`` in
the following section).

Release Marshal Buffer Handles
==============================

When you fetch a component object or create a new object, you get back a
handle to the object location. The handle is either an ``TSMLoc`` for an
object location or ``char *`` for a string location. You can manipulate
the object through these handles, but when you are finished you need to
release the handle to free up system resources.

The general guideline is to release all ``TSMLoc`` and string handles
you retrieve. The one exception is the string returned by
``TSUrlStringGet``, which must be freed by a call to ``TSfree``.

The handle release functions expect three arguments: the marshal buffer
containing the data, the location of the parent object, and the location
of the object to be released. The parent location is usually clear from
the creation of the ``TSMLoc`` or string. For example, if your plugin
had the following calls:

.. code-block:: c

   url_loc = TSHttpHdrUrlGet (bufp, hdr_loc);
   host_string = TSUrlHostGet (bufp, url_loc, &host_length);

then your plugin would have to call:

.. code-block:: c

   TSHandleMLocRelease (bufp, hdr_loc, url_loc);

If an ``TSMLoc`` is obtained from a transaction, then it does not have a
parent ``TSMLoc``. Use the null ``TSMLoc`` constant ``TS_NULL_MLOC`` as
its parent. For example, if your plugin calls:

.. code-block:: c

   TSHttpTxnClientReqGet (txnp, &bufp, &hdr_loc);

then you must release ``hdr_loc`` with:

.. code-block:: c

   TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);

You need to use ``TS_NULL_MLOC`` to release any ``TSMLoc`` handles
retrieved by the ``TSHttpTxn*Get`` functions.

Here's an example using a new ``TSMimeHdrField`` function:

.. code-block:: c

   TSHttpTxnServerRespGet( txnp, &resp_bufp, &resp_hdr_loc );
   new_field_loc = TSMimeHdrFieldCreate (resp_bufp, resp_hdr_loc);
   TSHandleMLocRelease ( resp_bufp, resp_hdr_loc, new_field_loc);
   TSHandleMLocRelease ( resp_bufp, TS_NULL_MLOC, resp_hdr_loc);

See the sample plugins for many more examples.

.. tip::

   You should release handles before reenabling the HTTP transaction.
   In other words, call ``TSHandleMLocRelease`` before ``TSHttpTxnReenable``.
