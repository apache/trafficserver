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


TSfwrite
========

Attempts to write length bytes of data from the buffer buf to the file
filep.


Synopsis
--------

`#include <ts/ts.h>`

.. c:function:: size_t TSfwrite(TSFile filep, const void *buf, size_t length)


Description
-----------

Make sure that filep is open for writing.  You might want to check the
number of bytes written (:c:func:`TSfwrite` returns this value)
against the value of length.  If it is less, there might be
insufficient space on disk, for example.
