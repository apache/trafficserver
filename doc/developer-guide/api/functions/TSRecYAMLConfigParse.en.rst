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

.. default-domain:: cpp

TSYAMLRecCfgFieldData
*********************

Parse a record yaml file.

Synopsis
========

.. code-block:: cpp

   #include <ts/ts.h>
   #include <ts/apidefs.h>

.. type:: TSYAMLRecCfgFieldData

   Configuration field info.

.. type:: TSReturnCode (*TSYAMLRecNodeHandler)(const TSYAMLRecCfgFieldData *cfg, void *data);

   Record field callback (called on every parsed record field(last node field on each YAML map))

.. function:: TSReturnCode TSRecYAMLConfigParse(TSYaml node, TSYAMLRecNodeHandler handler, void *data);

   Parse a YAML node following the record structure. Handler will be called on every final field.


Description
===========


   ``TSYAMLRecCfgFieldData``

   This contains information about the parsed record field. This will be passed back to the `TSYAMLRecNodeHandler`
   when you call `TSRecYAMLConfigParse`. This class holds the record name as well as the YAML node in case the caller
   wants to use this information to manipulate the record.
   The record name set is just a reflection of what was build from the YAML document parsing. This API will not perform any
   check against the internal ATS records. The record validation should be done by the caller, for instance, by calling `TSHttpTxnConfigFind`.

.. var:: const char* field_name

   A null-terminated string with the YAML field name. This holds the name of the scalar field. This is mostly to be used for logging.

.. var:: const char* record_name

    A null-terminated string with the record name which was built when parsing the YAML file. Example: `proxy.config.diags.debug.enabled`.
    Use this to validate or do any check base on a record name.

.. var:: TSYaml value_node

   A `YAML::Node` type holding the value of the field. Field value must be extracted from here. You can use the `YAML::Node` API to deduce
   the type, etc.

   :tye: TSYAMLRecNodeHandler

   Callback function for the caller to deal with each parsed node. ``cfg`` holds the details of the parsed field. `data` can be used to
   pass information along.

   :func: TSRecYAMLConfigParse

   Parse a YAML node following the record structure internals. On every scalar node the `handler` callback will be
   invoked with the appropriate parsed fields. `data` can be used to pass information along to every callback, this could be
   handy when you need to read/set data inside the `TSYAMLRecNodeHandler` to be read at a later stage.


Example:

   .. code-block:: yaml

      ts:
        plugin_x:
          field_1: 1
          data:
            data_field_1: XYZ


   `TSRecYAMLConfigParse` will parse the node and will call back on each field (`field_1` and `data_field_1`)


   A coding example using the above yaml file would look like:


   .. code-block:: cpp

      TSReturnCode
      field_handler(const TSYAMLRecCfgFieldData *cfg, void *data/*optional*/)
      {
         YAML::Node value = *reinterpret_cast<YAML::Node *>(cfg->value_node);

         /*
          Th callback function will be invoked twice:

          1:
            cfg->field_name = field_1
            cfg->record_name = ts.plugin_x.field_1

          2:
            cfg->field_name = data_field_1
            cfg->record_name = ts.plugin_x.data.data_field_1


          If we need to check the type, we either get the type from the YAML::Node::Tag if set, or from @c TSHttpTxnConfigFind
         */

         return TS_SUCCESS;
      }

      void my_plugin_function_to_parse_a_records_like_yaml_file(const char* path) {
         try {
            YAML::Node root = YAML::LoadFile(path);

            auto ret = TSRecYAMLConfigParse(reinterpret_cast<TSYaml>(&root), field_handler, nullptr);
            if (ret != TS_SUCCESS) {
               TSError("We found an error while parsing '%s'.", path.c_str());
               return;
            }
        } catch(YAML::Exception const&ex) {
          // :(
        }
      }
   ..


Return Values
=============

:func:`TSRecYAMLConfigParse`

:func:`TSRecYAMLConfigParse`  This will return :const:`TS_ERROR` if there was an issue while parsing the file. Particular node errors
should be handled by the `TSYAMLRecNodeHandler` implementation.

