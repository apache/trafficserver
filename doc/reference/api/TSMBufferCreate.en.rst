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

.. default-domain:: c

===============
TSMBufferCreate
===============

Traffic Server marshall buffer API.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSMBuffer TSMBufferCreate(void)
.. function:: TSReturnCode TSMBufferDestroy(TSMBuffer bufp)
.. function:: TSReturnCode TSHandleMLocRelease(TSMBuffer bufp, TSMLoc parent, TSMLoc mloc)

Description
===========

The marshal buffer or :type:`TSMBuffer` is a heap data structure that stores
parsed URLs, MIME headers and HTTP headers. You can allocate new objects
out of marshal buffers, and change the values within the marshal buffer.
Whenever you manipulate an object, you require the handle to the object
(:type:`TSMLoc`) and the marshal buffer containing the object (:type:`TSMBuffer`).

Any marshal buffer fetched by :func:`TSHttpTxn*Get` will be used by other parts
of the system. Be careful not to destroy these shared, transaction marshal buffers.

:func:`TSMBufferCreate` creates a new marshal buffer and initializes
the reference count. :func:`TSMBufferDestroy` Ignores the reference
count and destroys the marshal buffer bufp. The internal data buffer
associated with the marshal buffer is also destroyed if the marshal
buffer allocated it.

:func:`TSHandleMLocRelease` Releases the :type:`TSMLoc` mloc created
from the :type:`TSMLoc` parent. If a :type:`TSMLoc` is obtained from
a transaction, it does not have a parent :type:`TSMLoc`. Use the
the constant :data:`TS_NULL_MLOC` as its parent.

Return values
=============

:func:`TSMBufferDestroy` and :func:`TSHandleMLocRelease` return
:data:`TS_SUCCESS` on success, or :data:`TS_ERROR` on failure.
:func:`TSMBufferCreate` returns the new :type:`TSMBuffer`.

Examples
========

::

    #include <ts/ts.h>

    static void
    copyResponseMimeHdr (TSCont pCont, TSHttpTxn pTxn)
    {
        TSMBuffer respHdrBuf, tmpBuf;
        TSMLoc respHttpHdrLoc, tmpMimeHdrLoc;

        if (!TSHttpTxnClientRespGet(pTxn, &respHdrBuf, &respHttpHdrLoc)) {
            TSError("couldn't retrieve client response header0);
            TSHandleMLocRelease(respHdrBuf, TS_NULL_MLOC, respHttpHdrLoc);
            goto done;
        }

        tmpBuf = TSMBufferCreate();
        tmpMimeHdrLoc = TSMimeHdrCreate(tmpBuf);
        TSMimeHdrCopy(tmpBuf, tmpMimeHdrLoc, respHdrBuf, respHttpHdrLoc);
        TSHandleMLocRelease(tmpBuf, TS_NULL_MLOC, tmpMimeHdrLoc);
        TSHandleMLocRelease(respHdrBuf, TS_NULL_MLOC, respHttpHdrLoc);
        TSMBufferDestroy(tmpBuf);

    done:
        TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE);
    }

See also
========

:manpage:`TSAPI(3ts)`
