/** @file

  Configs for PreWarming Tunnel

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

#include "PreWarmConfig.h"
#include "PreWarmManager.h"

////
// PreWarmConfigParams
//
PreWarmConfigParams::PreWarmConfigParams()
{
  // RECU_RESTART_TS
  REC_EstablishStaticConfigByte(enabled, "proxy.config.tunnel.prewarm.enabled");
  REC_EstablishStaticConfigInteger(max_stats_size, "proxy.config.tunnel.prewarm.max_stats_size");

  // RECU_DYNAMIC
  REC_ReadConfigInteger(event_period, "proxy.config.tunnel.prewarm.event_period");
  REC_ReadConfigInteger(algorithm, "proxy.config.tunnel.prewarm.algorithm");
}

////
// PreWarmConfig
//
void
PreWarmConfig::startup()
{
  _config_update_handler = std::make_unique<ConfigUpdateHandler<PreWarmConfig>>();

  // dynamic configs
  _config_update_handler->attach("proxy.config.tunnel.prewarm.event_period");
  _config_update_handler->attach("proxy.config.tunnel.prewarm.algorithm");

  reconfigure();
}

void
PreWarmConfig::reconfigure()
{
  PreWarmConfigParams *params = new PreWarmConfigParams();
  _config_id                  = configProcessor.set(_config_id, params);

  prewarmManager.reconfigure();
}

PreWarmConfigParams *
PreWarmConfig::acquire()
{
  return static_cast<PreWarmConfigParams *>(configProcessor.get(_config_id));
}

void
PreWarmConfig::release(PreWarmConfigParams *params)
{
  configProcessor.release(_config_id, params);
}
