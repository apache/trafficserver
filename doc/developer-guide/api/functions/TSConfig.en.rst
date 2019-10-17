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

TSConfig Functions
******************

Synopsis
========

`#include <ts/ts.h>`

.. type:: void (*TSConfigDestroyFunc)(void*)
.. function:: unsigned int TSConfigSet(unsigned int id, void * data, TSConfigDestroyFunc funcp)
.. function:: TSConfig TSConfigGet(unsigned int id)
.. function:: void TSConfigRelease(unsigned int id, TSConfig configp)
.. function:: void* TSConfigDataGet(TSConfig configp)

Description
===========

These functions provide a mechanism to safely update configurations for a plugin.

If a plugin stores its configuration in a data structure, updating that structure due to changes in
the configuration file can be dangerous due to the asynchronous nature of plugin callbacks. To avoid
that problem these functions allow a plugin to register a configuration and then later replace it
with another instance without changing the instance in use by plugin callbacks. This works in the
same manner as `shared pointer <https://en.wikipedia.org/wiki/Smart_pointer>`__. When a plugin needs
to access the configuration data, it calls :func:`TSConfigGet` which returns a pointer that will
remain valid until the plugin calls :func:`TSConfigRelease`. The configuration instance is updated
with :func:`TSConfigSet` which will update an internal pointer but will not change any instance in
use via :func:`TSConfigGet`. When the last in use pointer is released, the old configuration is
destructed using the function passed to func:`TSConfigSet`. This handles the overlapping
configuration update case correctly.

Initialization is done with :func:`TSConfigSet`. The :arg:`id` should be zero on the first call.
This will allocate a slot for the configuration, which is returned. Subsequent calls must use this
value. If the :arg:`id` is not zero, that value is returned. In general, the code will look
something like ::

   class Config { ... }; // Configuration data.
   int Config_Id = 0;
   //...
   void ConfigUpdate() {
      Config* cfg = new Config;
      cfg.load(); // load config...
      Config_Id = TSConfigSet(Config_Id, cfg,
         [](void* cfg){delete static_cast<Config*>(config);});
   }
   //...
   PluginInit() {
      // ...
      ConfigUpdate();
      // ...
   }

:code:`Config_Id` is a global variable holding the configuration id, which is updated by the
return value of :func:`TSConfigSet`.

The configuration instance is retrieved with :func:`TSConfigGet`. This returns a wrapper class from
which the configuration can be extracted with :func:`TSConfigDataGet`. The wrapper is used to
release the configuration instance when it is no longer in use. The code in a callback tends to look
like ::

   int callback() {
      auto cfg_ref = TSConfigGet(Config_Id);
      Config* cfg = static_cast<Config*>(TSConfigDataGet(cfg_ref));
      // ... use cfg
      TSConfigRelease(Config_Id, cfg_ref);
   }
