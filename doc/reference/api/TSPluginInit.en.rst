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

.. default-domain:: c

============
TSPluginInit
============

Traffic Server plugin loading and registration.

Synopsis
========

`#include <ts/ts.h>`

.. function:: void TSPluginInit(int argc, const char* argv[])
.. function:: TSReturnCode TSPluginRegister(TSSDKVersion sdk_version, TSPluginRegistrationInfo* plugin_info)

Description
===========

:func:`TSPluginInit` must be defined by all plugins. Traffic Server
calls this initialization routine when it loads the plugin and sets
argc and argv appropriately based on the values in plugin.config.
argc is a count of the number of arguments in the argument vector,
argv. The count is at least one because the first argument in the
argument vector is the plugins name, which must exist in order for
the plugin to be loaded. argv is the vector of arguments. The number
of arguments in the vector is argc, and argv[0] always contains the
name of the plugin shared library.  :func:`TSPluginRegister` registers
the appropriate SDK version for your plugin.  Use this function to
make sure that the version of Traffic Server on which your plugin
is running supports the plugin.

Return values
=============

:func:`TSPluginRegister` returns :const:`TS_ERROR` if the plugin registration failed.

Examples
========

::

    #include <ts/ts.h>

    void
    TSPluginInit (int argc, const char *argv[])
    {
        TSPluginRegistrationInfo info;
        info.plugin_name = "hello-world";
        info.vendor_name = "MyCompany";
        info.support_email = "ts-api-support@MyCompany.com";

        if (TSPluginRegister(TS_SDK_VERSION_3_0 , &info) != TS_SUCCESS) {
            TSError("Plugin registration failed. 0);
        }
    }

See also
========

:manpage:`TSAPI(3ts)`, :manpage:`TSInstallDirGet(3ts)`
