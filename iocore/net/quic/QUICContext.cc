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

#include "QUICContext.h"
#include "QUICConnection.h"
#include "QUICLossDetector.h"
#include "QUICPacketProtectionKeyInfo.h"

class QUICCCConfigQCP : public QUICCCConfig
{
public:
  virtual ~QUICCCConfigQCP() {}
  QUICCCConfigQCP(const QUICConfigParams *params) : _params(params) {}

  uint32_t
  initial_window() const override
  {
    return this->_params->cc_initial_window();
  }

  uint32_t
  minimum_window() const override
  {
    return this->_params->cc_minimum_window();
  }

  float
  loss_reduction_factor() const override
  {
    return this->_params->cc_loss_reduction_factor();
  }

  uint32_t
  persistent_congestion_threshold() const override
  {
    return this->_params->cc_persistent_congestion_threshold();
  }

private:
  const QUICConfigParams *_params;
};

class QUICLDConfigQCP : public QUICLDConfig
{
public:
  virtual ~QUICLDConfigQCP() {}
  QUICLDConfigQCP(const QUICConfigParams *params) : _params(params) {}

  uint32_t
  packet_threshold() const override
  {
    return this->_params->ld_packet_threshold();
  }

  float
  time_threshold() const override
  {
    return this->_params->ld_time_threshold();
  }

  ink_hrtime
  granularity() const override
  {
    return this->_params->ld_granularity();
  }

  ink_hrtime
  initial_rtt() const override
  {
    return this->_params->ld_initial_rtt();
  }

private:
  const QUICConfigParams *_params;
};

QUICContext::QUICContext(QUICRTTProvider *rtt, QUICConnectionInfoProvider *info, QUICPacketProtectionKeyInfoProvider *key_info,
                         QUICPathManager *path_manager)
  : _key_info(key_info),
    _connection_info(info),
    _rtt_provider(rtt),
    _path_manager(path_manager),
    _ld_config(std::make_unique<QUICLDConfigQCP>(_config)),
    _cc_config(std::make_unique<QUICCCConfigQCP>(_config))
{
}

QUICConnectionInfoProvider *
QUICContext::connection_info() const
{
  return _connection_info;
}

QUICConfig::scoped_config
QUICContext::config() const
{
  return _config;
}

QUICPacketProtectionKeyInfoProvider *
QUICContext::key_info() const
{
  return _key_info;
}

QUICRTTProvider *
QUICContext::rtt_provider() const
{
  return _rtt_provider;
}

QUICLDConfig &
QUICContext::ld_config() const
{
  return *_ld_config;
}

QUICCCConfig &
QUICContext::cc_config() const
{
  return *_cc_config;
}

QUICPathManager *
QUICContext::path_manager() const
{
  return _path_manager;
}
