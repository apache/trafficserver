MIME Fields Always Belong to an Associated MIME Header
******************************************************

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
