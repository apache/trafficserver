/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
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
 */

#include "proxy/PluginHttpConnect.h"
#include "proxy/http/HttpSessionAccept.h"

extern HttpSessionAccept *plugin_http_accept;

PluginVC *
PluginHttpConnectInternal(TSHttpConnectOptions *options)
{
  if (options->buffer_index < TS_IOBUFFER_SIZE_INDEX_128 || options->buffer_index > MAX_BUFFER_SIZE_INDEX) {
    options->buffer_index = TS_IOBUFFER_SIZE_INDEX_32K; // out of range, set to the default for safety
  }

  if (options->buffer_water_mark < TS_IOBUFFER_WATER_MARK_PLUGIN_VC_DEFAULT) {
    options->buffer_water_mark = TS_IOBUFFER_WATER_MARK_PLUGIN_VC_DEFAULT;
  }

  if (plugin_http_accept) {
    PluginVCCore *new_pvc = PluginVCCore::alloc(plugin_http_accept, options->buffer_index, options->buffer_water_mark);

    new_pvc->set_active_addr(options->addr);
    new_pvc->set_plugin_id(options->id);
    new_pvc->set_plugin_tag(options->tag);

    PluginVC *return_vc = new_pvc->connect();

    if (return_vc != nullptr) {
      PluginVC *other_side = return_vc->get_other_side();

      if (other_side != nullptr) {
        other_side->set_is_internal_request(true);
      }
    }

    return return_vc;
  }

  return nullptr;
}
