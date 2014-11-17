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


TSSslAdvertiseProtocolSet
============

Synopsis
--------

`#include <ts/ts.h>`

.. c:function:: TSReturnCode TSSslAdvertiseProtocolSet(TSVConn sslp, const unsigned char ** list, unsigned int count);

Description
-----------

   Modifies the NPN advertisement list for a given SSL connection with :arg:`list`. If :arg:`count` is 0, sets the NPN advertisement list to the default registered protocol list for the end point. Note that, the plugin that uses this API owns the :arg:`list` and is responsible for making sure it points to a valid memory.

