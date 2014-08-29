Plugin Registration and Version Checking
****************************************

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

Make sure that the functions in your plugin are supported in your
version of Traffic Server.

Use the following interfaces:

-  `TSPluginRegister <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a6d7f514e70abaf097c4a3f1ba01f6df8>`_
-  `TSTrafficServerVersionGet <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a3ef91e01612ffdce6dd040f836db08e8>`_

The following version of ``hello-world`` registers the plugin and
ensures it's running with a compatible version of Traffic Server.

.. code-block:: c

    #include <stdio.h>
    #include <ts/ts.h>
    int
    check_ts_version()
    {

     const char *ts_version = TSTrafficServerVersionGet();
     int result = 0;

       if (ts_version) {
        int major_ts_version = 0;
        int minor_ts_version = 0;
        int patch_ts_version = 0;

       if (sscanf(ts_version, "%d.%d.%d", &major_ts_version,
          &minor_ts_version, &patch_ts_version) != 3) {
          return 0;
      }

      /* We need at least Traffic Server 2.0 */

       if (major_ts_version >= 2) {
          result = 1;
       }
       
      }

      return result;
    }

    void
    TSPluginInit (int argc, const char *argv[])
    {

          TSPluginRegistrationInfo info;

          info.plugin_name = "hello-world";
          info.vendor_name = "MyCompany";
          info.support_email = "ts-api-support@MyCompany.com";

          if (!TSPluginRegister (TS_SDK_VERSION_2_0 , &info)) {
             TSError ("Plugin registration failed. \n");
          }

          if (!check_ts_version()) {
             TSError ("Plugin requires Traffic Server 2.0 or later\n");
             return;
          }

          TSDebug ("debug-hello", "Hello World!\n");
    }

