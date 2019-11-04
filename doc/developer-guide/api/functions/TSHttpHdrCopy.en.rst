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

TSHttpHdrCopy
*************

Copies the contents of the HTTP header located at :arg:`src_loc` within
:arg:`src_bufp` to the HTTP header located at :arg:`dest_loc` within
:arg:`dest_bufp`.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpHdrCopy(TSMBuffer dest_bufp, TSMLoc dest_offset, TSMBuffer src_bufp, TSMLoc src_offset)

Description
===========

:c:func:`TSHttpHdrCopy` works correctly even if :arg:`src_bufp` and :arg:`dest_bufp`
point to different :ref:`developer-plugins-http-headers-marshal-buffers`. Make
sure that you create the destination HTTP header before copying into it.

.. note::

   :c:func:`TSHttpHdrCopy` appends the port number to the domain of
   the URL portion of the header. For example, a copy of
   http://www.example.com appears as http://www.example.com:80 in the
   destination buffer.
