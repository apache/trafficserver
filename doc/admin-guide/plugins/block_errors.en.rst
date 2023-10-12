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

.. _admin-plugins-block_errors:

Block Errors Plugin
*******************

Description
===========
The `block_errors` plugin blocks connections for clients that have too many HTTP/2 errors on the server.

The plugin tracks users based on their IP address and blocks them for a configurable amount of time.
The existing connection that experience errors and is over the error limit will be closed.  The plugin also supports on the fly configuration changes using the `traffic_ctl` command.


Configuration
=============

To enable the `block_errors` plugin, insert the following line in :file:`plugin.config`:

    block_errors.so

Additional configuration options are available and can be set in :file:`plugin.config`:

    block_errors.so <error limit> <timeout> <enable>

- ``error limit``: The number of errors allowed before blocking the client. Default: 1000 (per minute)
- ``timeout``: The time in minutes to block the client. Default: 4 (minutes)
- ``enable``: Enable (1) or disable (0) the plugin. Default: 1 (enabled)

Example Configuration
=====================

    block_errors.so 1000 4 0 1

Run Time Configuration
======================
The plugin can be configured at run time using the `traffic_ctl` command.  The following commands are available:

- ``block_errors.error_limit``: Set the error limit.  Takes a single argument, the number of errors allowed before blocking the client.
- ``block_errors.timeout``: Set the block timeout.  Takes a single argument, the number of minutes to block the client.
- ``block_errors.enabled``: Enable or disable the plugin.  Takes a single argument, 0 to disable, 1 to enable.

Example Run Time Configuration
==============================

    traffic_ctl plugin msg block_errors.error_limit 10000

    traffic_ctl plugin msg block_errors.timeout 10

    traffic_ctl plugin msg block_errors.enabled 1
