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

.. include:: ../../common.defs

.. _developer-plugins-configuration:

Plugin Configuration
********************

The functions discussed in this section do not examine or modify |TS|
configuration variables. To examine |TS| configuration and statistics
variables, see :ref:`developer-plugins-management-settings-and-statistics`.

The collection of ``TSConfig*`` functions are designed to provide a fast and
efficient mechanism for accessing and changing global configuration information
within a plugin. Such a mechanism is simple enough to provide in a
single-threaded program, but the translation to a multi-threaded program such
as |TS| is difficult. A common technique is to have a single mutex protect the
global configuration information; however, the problem with this solution is
that a single mutex becomes a performance bottleneck very quickly.

These functions define an interface to storing and retrieving an opaque data
pointer. Internally, |TS| maintains reference count information about the data
pointer so that a call to :c:func:`TSConfigSet` will not disturb another thread
using the current data pointer. The philosophy is that once a user has a hold
of the configuration pointer, it is okay for it to be used even if the
configuration changes. All that a user typically wants is a non-changing
snapshot of the configuration. You should use :c:func:`TSConfigSet` for all
global data updates.

Here's how the interface works:

.. code-block:: c

    /* Assume that you have previously defined a plugin configuration
     * data structure named ConfigData, along with its constructor
     * plugin_config_allocator () and its destructor 
     * plugin_config_destructor (ConfigData *data)
     */
    ConfigData *plugin_config;

    /* You will need to assign plugin_config a unique identifier of type
     * unsigned int. It is important to initialize this identifier to zero
     * (see the documentation of the  function). 
     */
    static unsigned int   my_id = 0;

    /* You will need an TSConfig pointer to access a snapshot of the 
     * current plugin_config. 
     */
    TSConfig config_ptr;

    /* Initialize plugin_config. */
    plugin_config = plugin_config_allocator();

    /* Assign plugin_config an identifier using TSConfigSet. */
    my_id = TSConfigSet (my_id, plugin_config, plugin_config_destructor);

    /* Get a snapshot of the current configuration using TSConfigGet. */
    config_ptr = TSConfigGet (my_id);

    /* With an TSConfig pointer to the current configuration, you can 
     * retrieve the configuration's current data using TSConfigDataGet. 
     */
    plugin_config = (ConfigData*) TSConfigDataGet (config_ptr);

    /* Do something with plugin_config here. */

    /* When you are done with retrieving or modifying the plugin data, you
     * release the pointers to the data with a call to TSConfigRelease.
     */
    TSConfigRelease (my_id, config_ptr);

    /* Any time you want to modify plugin_config, you must repeat these
     * steps, starting with 
     * my_id = TSConfigSet (my_id,plugin_config, plugin_config_destructor);
     * and continuing up to TSConfigRelease. 
     */

The configuration functions are:

-  :c:func:`TSConfigDataGet`

-  :c:func:`TSConfigGet`

-  :c:func:`TSConfigRelease`

-  :c:func:`TSConfigSet`


