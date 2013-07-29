Release Marshal Buffer Handles
******************************

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
