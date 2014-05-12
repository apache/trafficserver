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


TSMimeHdrCopy
=============

Copies the contents of the MIME header located at src_loc within
src_bufp to the MIME header located at dest_loc within dest_bufp.


Synopsis
--------

`#include <ts/ts.h>`

.. c:function:: TSReturnCode TSMimeHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_offset, TSMBuffer src_bufp, TSMLoc src_offset)


Description
-----------

:c:func:`TSMimeHdrCopy` works correctly even if src_bufp and dest_bufp
point to different marshal buffers.

.. important::

   you must create the destination MIME header before copying into
   it--use :c:func:`TSMimeHdrCreate`.
