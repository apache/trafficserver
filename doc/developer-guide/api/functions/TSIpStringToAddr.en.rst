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

TSIpStringToAddr
****************

Synopsis
========

`#include <ts/experimental.h>`

.. function:: TSReturnCode TSIpStringToAddr(const char * str, int str_len, sockaddr* addr)

Description
===========

:arg:`str` is expected to be an explicit address, not a hostname.  No hostname resolution is done. This attempts to
recognize and process a port value if present. It is set appropriately, or to zero if no port was found or it was
malformed.

It is intended to deal with the brackets that can optionally surround an IP address (usually IPv6) which in turn are
used to differentiate between an address and an attached port. E.g.

.. code-block:: none

[FE80:9312::192:168:1:1]:80

Return values
=============

It returns :data:`TS_SUCCESS` on success, or :data:`TS_ERROR` on failure.

Notes
=====

This API may be changed in the future version since it is experimental.

