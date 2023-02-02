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

.. include:: ../../../common.defs

.. _developer-plugins-getting-started-simple-plugin:

A Simple Plugin
***************

This section describes how to write, compile, configure, and run a
simple Traffic Server plugin. You'll follow the steps below:

1. Make sure that your plugin source code contains an :c:func:`TSPluginInit`
   initialization function.

2. Compile your plugin source code, creating a shared library.

3. Add an entry to :file:`plugin.config`.

4. Add the path to your plugin shared library into :file:`records.yaml`.

5. Restart Traffic Server.

Compile Your Plugin
===================

The process for compiling a shared library varies with the platform
used, so the Traffic Server API provides the tsxs tool which you can use
to create shared libraries on all the supported Traffic Server
platforms.

Example
-------

Assuming the sample program is stored in the file ``hello_world.c``, you
could use the following commands to build a shared library::

    tsxs -o hello_world.so -c hello_world.c

``tsxs`` is installed in the ``bin`` directory of |TS|.

This shared library will be your plugin. In order to install it, run::

    sudo tsxs -o hello_world.so -i

or the equivalent to ``sudo`` on your platform.

Update the plugin configuration file
====================================

Your next step is to tell Traffic Server about the plugin by adding the
following line to :file:`plugin.config` file. Since our simple plugin
does not require any arguments, the following :file:`plugin.config` will
work::

    # a simple plugin.config for hello_world
    hello_world.so

|TS| can accommodate multiple plugins. If several plugin
functions are triggered by the same event, then Traffic Server invokes
each plugin's function in the order each was defined in :file:`plugin.config`.

.. _specify-the-plugins-location:

Specify the Plugin's Location
=============================

All plugins must be located in the directory specified by the
configuration variable ``proxy.config.plugin.plugin_dir``, which is
located in the :file:`records.yaml` file. The directory can be specified
as an absolute or relative path.

If a relative path is used, then the starting directory will be the
Traffic Server installation directory as specified in
``etc/traffic_server``. The default value is ``libexec/trafficserver``,
but this can vary based on how the software was configured and built. It
is common to use the default directory. Be sure to place the shared
library ``hello_world.so`` inside the directory you've configured.

Restart Traffic Server
======================

The last step is to start/restart Traffic Server. Shown below is the
output displayed after you've created and loaded your ``hello_world``
plugin.

::

    # ls libexec/trafficserver
    hello_world.so*
    # bin/traffic_server
    [Mar 27 19:06:31.669] NOTE: updated diags config
    [Mar 27 19:06:31.680] NOTE: loading plugin 'libexec/trafficserver/hello_world.so'
    hello world
    [Mar 27 19:06:32.046] NOTE: cache disabled (initializing)
    [Mar 27 19:06:32.053] NOTE: cache enabled
    [Mar 27 19:06:32.526] NOTE: Traffic Server running

.. note::

  In the example above, Traffic Server notes are directed to the
  console by specifying ``E`` for :ts:cv:`proxy.config.diags.output.note` in
  :file:`records.yaml`. The second note shows Traffic Server attempting to
  load the ``hello_world`` plugin. The third line of Traffic Server output
  is from your plugin.
