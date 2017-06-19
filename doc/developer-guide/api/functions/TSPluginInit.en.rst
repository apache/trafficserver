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

TSPluginInit
************

Traffic Server plugin loading and registration.

Synopsis
========

`#include <ts/ts.h>`

.. function:: void TSPluginInit(int argc, const char* argv[])
.. function:: TSReturnCode TSPluginRegister(TSPluginRegistrationInfo* plugin_info)

Description
===========

:func:`TSPluginInit` must be defined by all plugins. Traffic Server
calls this initialization routine when it loads the plugin and sets
:arg:`argc` and :arg:`argv` appropriately based on the values in
:file:`plugin.config`.

:arg:`argc` is a count of the number of arguments in the argument vector,
:arg:`argv`. The count is at least one because the first argument in the
argument vector is the plugins name, which must exist in order for
the plugin to be loaded.

:func:`TSPluginRegister` registers the appropriate SDK version specific in
:arg:`sdk_version` for your plugin. Use this function to make sure that the
version of Traffic Server on which your plugin is running supports the plugin.

Return Values
=============

:func:`TSPluginRegister` returns :const:`TS_ERROR` if the plugin registration
failed.

Examples
========

::

   #include <ts/ts.h>
   #define PLUGIN_NAME "hello_world"

   void
   TSPluginInit (int argc, const char * argv[])
   {
      TSPluginRegistrationInfo info;
      info.plugin_name = PLUGIN_NAME;
      info.vendor_name = "MyCompany";
      info.support_email = "ts-api-support@MyCompany.com";

      if (TSPluginRegister(&info) != TS_SUCCESS) {
         TSError("[%s] Plugin registration failed", PLUGIN_NAME);
      }
   }

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSInstallDirGet(3ts)`
