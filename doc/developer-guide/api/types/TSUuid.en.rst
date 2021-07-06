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

TSUuid
******

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. c:type:: TSUuid

An opaque pointer to an internal representation of a UUID object.


Description
===========

The UUID object is managed and created via the various UUID APIs. You can not
access nor modify this object using anything but the provided APIs.

See Also
========

:manpage:`TSUuidCreate(3ts)`,
:manpage:`TSUuidInitialize(3ts)`,
:manpage:`TSUuidDestroy(3ts)`,
:manpage:`TSUuidCopy(3ts)`,
:manpage:`TSUuidStringGet(3ts)`,
:manpage:`TSUuidVersionGet(3ts)`,
:manpage:`TSUuidStringParse(3ts)`,
:manpage:`TSProcessUuidGet(3ts)`
