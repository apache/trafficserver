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

TSYAMLCompatibilityCheck
************************

Get the YAML-CPP library version used in core

Synopsis
========

.. code-block:: cpp

   #include <ts/ts.h>

.. c:var::  const char* TS_YAML_CPP_CORE_VERSION;
.. c:var::  int TS_YAML_CPP_CORE_VERSION_LEN;
.. function:: TSReturnCode TSYAMLCompatibilityCheck(const char *yamlcpp_lib_version, int yamlcpp_lib_len);

Description
===========

There could be a mismatch between YAML-CPP versions used in Core and  Plugins, if a particular Plugin uses a TS API
that have a ``TSYaml`` type in their signature then they should make sure they use the same version to avoid any binary
compatibility issue. This API is supplied to let Plugins perform this check.

:var:`TS_YAML_CPP_CORE_VERSION` Can be used to read the YAML-CPP version used in core.
:var:`TS_YAML_CPP_CORE_VERSION_LEN` Size of the string representation of the YAML-CPP version.
:type:`TSYAMLCompatibilityCheck` Checks the passed YAML-CPP library version against the used(built) in core.
:arg:`yamlcpp_lib_version` should be a string with the yaml-cpp library version the plugin is using. A null terminated string is
expected.
:arg:`yamlcpp_lib_len` size of the passed string. If yamlcpp_lib_len is -1 then TSYAMLCompatibilityCheck() assumes that value is
null-terminated. Otherwise, the length of the string value is taken to be length.


Example:

   .. code-block:: cpp

         #include <ts/ts.h>

         std::string_view my_yaml_version {"0.7.0"};
         if (TSYAMLCompatibilityCheck(my_yaml_version.c_str(), my_yaml_version.size()) != TS_SUCCESS) {
            TSDebug("my_plugin", "Error: %.*s  is used in core.", TS_YAML_CPP_CORE_VERSION_LEN, TS_YAML_CPP_CORE_VERSION);
         }


Return Values
=============


:c:func:`TSYAMLCompatibilityCheck` Returns :const:`TS_SUCCESS` if no issues, :const:`TS_ERROR` if the :arg:`yamlcpp_lib_version`
was not set, or the ``yamlcpp`` version does not match with the one used in the core.
