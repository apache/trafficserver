.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

TSTrafficServerVersionGet
*************************

Return Traffic Server version information.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: const char * TSTrafficServerVersionGet(void)
.. function:: int TSTrafficServerVersionGetMajor(void)
.. function:: int TSTrafficServerVersionGetMinor(void)
.. function:: int TSTrafficServerVersionGetPatch(void)

Description
===========

:func:`TSTrafficServerVersionGet` returns a pointer to a string of characters
that indicates the Traffic Server release version. This string must not
be modified.

The other APIs return an integer from the relevant component of the version
number string.

Example
=======

::

    #include <stdio.h>
    #include <ts/ts.h>

    #define PLUGIN_NAME "hello_world"

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

            /* We need at least Traffic Server 3.0 */
            if (major_ts_version >= 3) {
                result = 1;
            }
        }

        return result;
    }

    void
    TSPluginInit (int argc, const char *argv[])
    {
        TSPluginRegistrationInfo info;

        info.plugin_name = PLUGIN_NAME;
        info.vendor_name = "MyCompany";
        info.support_email = "ts-api-support@MyCompany.com";

        if (TSPluginRegister(&info) != TS_SUCCESS) {
            TSError("[%s] Plugin registration failed", PLUGIN_NAME);
        }

        if (!check_ts_version()) {
            TSError("[%s] Plugin requires Traffic Server 3.0 or later", PLUGIN_NAME);
            return;
        }

        TSDebug(PLUGIN_NAME, "Hello World!");
    }

See Also
========

:manpage:`TSAPI(3ts)`
