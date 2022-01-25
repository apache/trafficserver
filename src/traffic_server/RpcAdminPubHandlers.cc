/**
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

#include "rpc/jsonrpc/JsonRPC.h"

// Admin API Implementation headers.
#include "rpc/handlers/config/Configuration.h"
#include "rpc/handlers/records/Records.h"
#include "rpc/handlers/storage/Storage.h"
#include "rpc/handlers/server/Server.h"
#include "rpc/handlers/plugins/Plugins.h"

#include "RpcAdminPubHandlers.h"
namespace rpc::admin
{
void
register_admin_jsonrpc_handlers()
{
  // Config
  using namespace rpc::handlers::config;
  rpc::add_method_handler("admin_config_set_records", &set_config_records, &core_ats_rpc_service_provider_handle);
  rpc::add_method_handler("admin_config_reload", &reload_config, &core_ats_rpc_service_provider_handle);

  // Records
  using namespace rpc::handlers::records;
  rpc::add_method_handler("admin_lookup_records", &lookup_records, &core_ats_rpc_service_provider_handle);
  rpc::add_method_handler("admin_clear_all_metrics_records", &clear_all_metrics_records, &core_ats_rpc_service_provider_handle);
  rpc::add_method_handler("admin_clear_metrics_records", &clear_metrics_records, &core_ats_rpc_service_provider_handle);

  // plugin
  using namespace rpc::handlers::plugins;
  rpc::add_method_handler("admin_plugin_send_basic_msg", &plugin_send_basic_msg, &core_ats_rpc_service_provider_handle);

  // server
  using namespace rpc::handlers::server;
  rpc::add_method_handler("admin_server_start_drain", &server_start_drain, &core_ats_rpc_service_provider_handle);
  rpc::add_method_handler("admin_server_stop_drain", &server_stop_drain, &core_ats_rpc_service_provider_handle);
  rpc::add_notification_handler("admin_server_shutdown", &server_shutdown, &core_ats_rpc_service_provider_handle);
  rpc::add_notification_handler("admin_server_restart", &server_shutdown, &core_ats_rpc_service_provider_handle);

  // storage
  using namespace rpc::handlers::storage;
  rpc::add_method_handler("admin_storage_set_device_offline", &set_storage_offline, &core_ats_rpc_service_provider_handle);
  rpc::add_method_handler("admin_storage_get_device_status", &get_storage_status, &core_ats_rpc_service_provider_handle);
}
} // namespace rpc::admin