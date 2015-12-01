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

TSfgets
*******

Synopsis
========

`#include <ts/ts.h>`

.. function:: char* TSfgets(TSFile filep, char * buf, size_t length)

Description
===========

Reads a line from the file pointed to by :arg:`filep` into the buffer
:arg:`buf`.

Lines are terminated by a line feed character, ' '.  The line placed
in the buffer includes the line feed character and is terminated with
a ``NULL``.  If the line is longer than length bytes then only the first
length-minus-1 bytes are placed in :arg:`buf`.
