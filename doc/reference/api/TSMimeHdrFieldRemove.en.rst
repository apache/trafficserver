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


TSMimeHdrFieldRemove
====================

Removes the MIME field located at field within bufp from the header
located at hdr within bufp.


Synopsis
--------

`#include <ts/ts.h>`

.. c:function:: TSReturnCode TSMimeHdrFieldRemove(TSMBuffer bufp, TSMLoc hdr, TSMLoc field)


Description
-----------

If the specified field cannot be found in the list of fields
associated with the header then nothing is done.

.. note::

   removing the field does not destroy the field, it only detaches the
   field, hiding it from the printed output.  The field can be
   reattached with a call to :c:func:`TSMimeHdrFieldAppend`.  If you
   do not use the detached field you should destroy it with a call to
   :c:func:`TSMimeHdrFieldDestroy` and release the handle field with a
   call to :c:func:`TSHandleMLocRelease`.
