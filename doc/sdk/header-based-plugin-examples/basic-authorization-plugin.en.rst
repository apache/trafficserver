The Basic Authorization Plugin
******************************

.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at
 
   http://www.apache.org/licenses/LICENSE-2.0
 
  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.

The sample basic authorization plugin, ``basic-auth.c``, checks for
basic HTTP proxy authorization. In HTTP basic proxy authorization,
client user names and passwords are contained in the
``Proxy-Authorization`` header. The password is encoded using base64
encoding. The plugin checks all incoming requests for the authorization
header, user name, and password. If the plugin does not find all of the
these, then it reenables with an error (effectively stopping the
transaction) and adds a transaction hook to the send response header
event.

Creating the Plugin's Parent Continuation and Global Hook
=========================================================

The parent continuation and global hook are created as follows:

``TSHttpHookAdd (TS_HTTP_OS_DNS_HOOK, TSContCreate (auth_plugin, NULL));``

.. toctree::
   :maxdepth: 2

   basic-authorization-plugin/implementing-the-handler-and-getting-a-handle-to-the-transaction.en
   basic-authorization-plugin/working-with-http-headers.en
   basic-authorization-plugin/setting-a-transaction-hook.en

