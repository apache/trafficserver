/** @file

  an example cert update plugin

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

#include <stdio.h>
#include <cstring>
#include <string>
#include <string_view>

#include "ts/ts.h"
#include "tscpp/util/TextView.h"

#define PLUGIN_NAME "cert_update"

// Plugin Message Continuation
int
CB_cert_update(TSCont, TSEvent, void *edata)
{
  TSPluginMsg *msg = static_cast<TSPluginMsg *>(edata);
  static constexpr std::string_view PLUGIN_PREFIX("cert_update."_sv);

  std::string_view tag(msg->tag, strlen(msg->tag));
  const char *server_cert_path = nullptr;
  const char *client_cert_path = nullptr;
  if (tag.substr(0, PLUGIN_PREFIX.size()) == PLUGIN_PREFIX) {
    tag.remove_prefix(PLUGIN_PREFIX.size());
    if (tag == "server") {
      server_cert_path = static_cast<const char *>(msg->data);
      TSDebug(PLUGIN_NAME, "Received Msg to update server cert with %s", server_cert_path);
    } else if (tag == "client") {
      client_cert_path = static_cast<const char *>(msg->data);
      TSDebug(PLUGIN_NAME, "Received Msg to update client cert with %s", client_cert_path);
    }
  }

  if (server_cert_path) {
    if (TS_SUCCESS == TSSslServerCertUpdate(server_cert_path, nullptr)) {
      TSDebug(PLUGIN_NAME, "Successfully updated server cert with %s", server_cert_path);
    } else {
      TSDebug(PLUGIN_NAME, "Failed to update server cert with %s", server_cert_path);
    }
  }
  if (client_cert_path) {
    if (TS_SUCCESS == TSSslClientCertUpdate(client_cert_path, nullptr)) {
      TSDebug(PLUGIN_NAME, "Successfully updated client cert with %s", client_cert_path);
    } else {
      TSDebug(PLUGIN_NAME, "Failed to update client cert with %s", client_cert_path);
    }
  }
  return TS_SUCCESS;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }
  TSDebug(PLUGIN_NAME, "Initialized.");
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, TSContCreate(CB_cert_update, nullptr));
}
