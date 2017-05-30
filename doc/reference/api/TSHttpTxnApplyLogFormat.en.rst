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


TSHttpTxnApplyLogFormat
=======================

Synopsis
--------

`#include <ts/ts.h>`

.. c:function:: TSReturnCode TSHttpTxnApplyLogFormat(TSHttpTxn txnp, char const* format, TSIOBuffer out)


Description
-----------

Apply the log :arg:`format` string to the transaction :arg:`txnp` placing the result in the buffer
:arg:`out`. The format is identical to :ref:`custom log formats <custom-logging-fields>`. The result
is appended to :arg:`out` in ASCII format. :arg:`out` must be created by the plugin.

See also
========

:manpage:`TSIOBufferCreate(3ts)`, :manpage:`TSIOBufferReaderAlloc(3ts)`

