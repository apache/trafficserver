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

TSHttpHdrStatusSet
******************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpHdrStatusSet(TSMBuffer bufp, TSMLoc offset, TSHttpStatus status)

.. function:: TSReturnCode TSHttpHdrStatusSet(TSMBuffer bufp, TSMLoc offset, TSHttpStatus status, std::string_view setter)

Description
===========

Sets the HTTP status code on an existing HTTP header object. An overload also
accepts the transaction and an identifying setter label. When provided, the
setter is recorded on the transaction for logging via the `prscs` log field.

Parameters
==========

- bufp: Marshal buffer containing the HTTP header.
- offset: Location of the HTTP header within bufp.
- status: The HTTP status code to set.
- setter: Optional label identifying the component setting the status; pass empty to leave unchanged.
