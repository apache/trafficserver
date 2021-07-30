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

TSMgmtConfigFileAdd
*******************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSMgmtConfigFileAdd(const char *parent, const char *fileName)

Description
===========

This is used to 'attach' a config file to a parent config file. It is meant to be used in the context
of a plugin but can also be used internally. Using this you can tie a plugin's config file to, for example,
remap.config. In that instance any changes to the fileName file will trigger a reload of the parent file when
a config reload is requested.

In the case of a remap.config reload all parent-child file associations are destroyed on reload but plugins are also
reloaded, so if using it within a plugin the API should be called in a location that will be known to be called on
plugin initialization.