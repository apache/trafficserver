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

.. _admin-plugins-healthchecks:

Health Checks Plugin
********************

This is a simple plugin, to provide basic (but configurable) health checks.
This is a server intercept plugin, and it takes one single configuration
option in plugin.config, the configuration file name.

Configuration
=============

To enable the healthchecks plugin, insert the following line in
:file:`plugin.config`::

    healthchecks.so <healthcheck-configuration-file>

The required ``<healthcheck-configuration-file>`` may reference either an
absolute or relative path to the file containing the healthcheck configuration.

This configuration may contain one or more lines of the format::

   <URI-path> <file-path> <mime> <file-exists-code> <file-missing-code>

.. note:: The ``URI-path`` can *not* be "/" only.

.. note:: This configuration is *not* reloadable.

The content of the file specified in the ``file-path``, if any, is sent as the 
body of the response. The existence of the file is sufficient to get an "OK"
status.  Performance wise, everything is served out of memory, and it only
stats / opens files as necessary. However, the content of the status file is
limited to 16KB, so this is not a generic static file serving plugin.

Example
=======

This line would define a health check link available at
http://www.example.com/__hc that would check if the file
``/var/run/ts-alive`` existed on the server.  If the file exists,
a response is built with the contents of the ``ts-alive`` file, a mime
type of ``text/plain`` and a status code of ``200``.  If the file does not
exist, a ``403`` response is sent::

   /__hc  /var/run/ts-alive  text/plain 200  403



