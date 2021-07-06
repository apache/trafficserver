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

TSPluginDSOReloadEnable
*************************

Control whether this plugin will take part in the remap dynamic reload process (remap.config)

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSPluginDSOReloadEnable(int enabled)

Description
===========

This function provides the ability to enable/disable programmatically the plugin
dynamic reloading when the same Dynamic Shared Object (DSO) is also used as a remap plugin.
This overrides :ts:cv:`proxy.config.plugin.dynamic_reload_mode`.

.. warning::  This function should be called from within :func:`TSPluginInit`

The function will return :type:`TS_ERROR`  in any of the following cases:
    - The function was not called from within :func:`TSPluginInit`
    - TS is unable to get the canonical path from the plugin's path.

See Also
========

:manpage:`TSAPI(3ts)`
