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

.. default-domain:: c

===============
TSInstallDirGet
===============

Return Traffic Server installation directories.

Synopsis
========

`#include <ts/ts.h>`

.. function:: const char * TSInstallDirGet(void)
.. function:: const char * TSConfigDirGet(void)
.. function:: const char * TSPluginDirGet(void)

Description
===========

:func:`TSInstallDirGet` returns the path to the root of the Traffic
Server installation. :func:`TSConfigDirGet` and :func:`TSPluginDirGet`
return the complete, absolute path to the configuration directory
and the plugin installation directory respectively.

Return values
=============

These functions all return a NUL-terminated string that must not be modified or freed.

Examples
========

To load a file that is located in the Traffic Server configuration directory::

    #include <ts/ts.h>
    #include <stdio.h>

    char * path;
    asprintf(&path, "%s/example.conf", TSConfigDirGet());

See also
========
:manpage:`TSAPI(3ts)`
