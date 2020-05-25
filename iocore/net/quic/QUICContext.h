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
#include "QUICEvents.h"

class QUICRTTProvider;
class QUICCongestionController;
class QUICPacketProtectionKeyInfoProvider;
class QUICPathManager;

class QUICNetVConnection;

class QUICContext : public QUICEventTrigger, public QUICEventRegister
{
public:
  virtual ~QUICContext(){};
  virtual QUICConnectionInfoProvider *connection_info() const   = 0;
  virtual QUICConfig::scoped_config config() const              = 0;
  virtual QUICLDConfig &ld_config() const                       = 0;
  virtual QUICPacketProtectionKeyInfoProvider *key_info() const = 0;
  virtual QUICCCConfig &cc_config() const                       = 0;
  virtual QUICRTTProvider *rtt_provider() const                 = 0;
  virtual QUICPathManager *path_manager() const                 = 0;
};

class QUICContextImpl : public QUICContext
{
public:
  QUICContextImpl(QUICRTTProvider *rtt, QUICConnectionInfoProvider *info, QUICPacketProtectionKeyInfoProvider *key_info,
                  QUICPathManager *path_manager);

  virtual QUICConnectionInfoProvider *connection_info() const override;
  virtual QUICConfig::scoped_config config() const override;
  virtual QUICRTTProvider *rtt_provider() const override;

  // TODO should be more abstract
  virtual QUICPacketProtectionKeyInfoProvider *key_info() const override;

  virtual QUICLDConfig &ld_config() const override;
  virtual QUICCCConfig &cc_config() const override;

  virtual QUICPathManager *path_manager() const override;

  // regist event processor
  virtual void regist_frame_receive_event(QUICFrameReceiveFunc &&) override;
  virtual void regist_packet_receive_event(QUICPacketReceiveFunc &&) override;
  virtual void regist_packet_send_event(QUICPacketSendFunc &&) override;
  virtual void regist_packet_lost_event(QUICPacketLostFunc &&) override;

  // trigger event
  virtual QUICConnectionErrorUPtr trigger_frame_receive_event(QUICEncryptionLevel, QUICFrame &) override;
  virtual QUICConnectionErrorUPtr trigger_packet_receive_event(QUICEncryptionLevel, QUICPacket &) override;
  virtual QUICConnectionErrorUPtr trigger_packet_send_event(QUICEncryptionLevel, QUICPacket &) override;
  virtual QUICConnectionErrorUPtr trigger_packet_lost_event(QUICEncryptionLevel, QUICPacket &) override;

private:
  QUICConfig::scoped_config _config;
  QUICPacketProtectionKeyInfoProvider *_key_info = nullptr;
  QUICConnectionInfoProvider *_connection_info   = nullptr;
  QUICRTTProvider *_rtt_provider                 = nullptr;
  QUICPathManager *_path_manager                 = nullptr;

  std::unique_ptr<QUICLDConfig> _ld_config = nullptr;
  std::unique_ptr<QUICCCConfig> _cc_config = nullptr;

  std::vector<QUICFrameReceiveFunc> _frame_receive_funcs;
  std::vector<QUICPacketSendFunc> _packet_send_funcs;
  std::vector<QUICPacketReceiveFunc> _packet_recv_funcs;
  std::vector<QUICPacketLostFunc> _packet_lost_funcs;
};
