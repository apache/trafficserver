Roadmap for Creating Plugins
****************************

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

This chapter has provided an overview of Traffic Server's HTTP
processing, API hooks, and the asynchronous event model. Next, you must
understand the capabilities of Traffic Server API functions. These are
quite broad:

-  **HTTP header manipulation functions**

   Obtain information about and manipulate HTTP headers, URLs, & MIME
   headers.

-  **HTTP transaction functions**

   Get information about and modify HTTP transactions (for example: get
   the client IP associated to the transaction; get the server IP; get
   parent proxy information)

-  **IO functions**

   Manipulate vconnections (virtual connections, used for network and
   disk I/O)

-  **Network connection functions**

   Open connections to remote servers.

-  **Statistics functions**

   Define and compute statistics for your plugin's activity.

-  **Traffic Server management functions**

   Obtain values for Traffic Server configuration and statistics
   variables.

Below are some guidelines for creating a plugin:

1. Decide what you want your plugin to do, based on the capabilities of
   the API and Traffic Server. Two main kinds of example plugins
   provided with this SDK are HTTP-based (includes header-based and
   response transform plugins), and non-HTTP-based (a protocol plugin).
   These examples are discussed in the next three chapters.

2. Determine where your plugin needs to hook on to Traffic Server's HTTP
   processing (view the :ref:`http-txn-state-diagram`

3. Read :doc:`../header-based-plugin-examples.en` to learn the basics of
   writing plugins: creating continuations and setting up hooks. If you
   want to write a plugin that transforms data, then read
   :doc:`../http-transformation-plugin.en`

4. Figure out what parts of the Traffic Server API you need to use and
   then read about the details of those APIs in this manual's reference
   chapters.

5. Compile and load your plugin (see :doc:`../getting-started.en`

6. Depending on your plugin's functionality, you might start testing it
   by issuing requests by hand and checking for the desired behavior in
   Traffic Server log files. See the ***Traffic Server Administrator's
   Guide*** for information about Traffic Server logs.

7. You can test the performance of Traffic Server running with your
   plugin using SDKTest. You can also customize SDKTest to perform
   functional testing on your plugin; for more information see the
   ***Traffic Server SDKTest User's Guide***.

