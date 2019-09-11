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

#pragma once

#include "QUICConnection.h"
#include "QUICConfig.h"

class QUICRTTProvider;
class QUICCongestionController;
class QUICPacketProtectionKeyInfoProvider;

class QUICNetVConnection;

class QUICContext
{
public:
  virtual ~QUICContext(){};
  virtual QUICConnectionInfoProvider *connection_info() const = 0;
  virtual QUICConfig::scoped_config config() const            = 0;
};

class QUICLDContext
{
public:
  virtual ~QUICLDContext() {}
  virtual QUICConnectionInfoProvider *connection_info() const   = 0;
  virtual QUICLDConfig &ld_config() const                       = 0;
  virtual QUICPacketProtectionKeyInfoProvider *key_info() const = 0;
};

class QUICCCContext
{
public:
  virtual ~QUICCCContext() {}
  virtual QUICConnectionInfoProvider *connection_info() const = 0;
  virtual QUICCCConfig &cc_config() const                     = 0;
  virtual QUICRTTProvider *rtt_provider() const               = 0;
};

class QUICContextImpl : public QUICContext, public QUICCCContext, public QUICLDContext
{
public:
  QUICContextImpl(QUICRTTProvider *rtt, QUICConnectionInfoProvider *info, QUICPacketProtectionKeyInfoProvider *key_info);

  virtual QUICConnectionInfoProvider *connection_info() const override;
  virtual QUICConfig::scoped_config config() const override;
  virtual QUICRTTProvider *rtt_provider() const override;

  // TODO should be more abstract
  virtual QUICPacketProtectionKeyInfoProvider *key_info() const override;

  virtual QUICLDConfig &ld_config() const override;
  virtual QUICCCConfig &cc_config() const override;

private:
  QUICConfig::scoped_config _config;
  QUICPacketProtectionKeyInfoProvider *_key_info = nullptr;
  QUICConnectionInfoProvider *_connection_info   = nullptr;
  QUICRTTProvider *_rtt_provider                 = nullptr;

  std::unique_ptr<QUICLDConfig> _ld_config = nullptr;
  std::unique_ptr<QUICCCConfig> _cc_config = nullptr;
};
