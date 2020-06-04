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
class QUICPacketR;

class QUICNetVConnection;
struct QUICPacketInfo;

// this class is a connection between the callbacks. it should do something
// TODO: it should do something
class QUICCallbackContext
{
};

class QUICCallback
{
public:
  virtual ~QUICCallback() {}

  // callback on connection close event
  virtual void connection_close_callback(QUICCallbackContext &){};
  // callback on packet send event
  virtual void packet_send_callback(QUICCallbackContext &, const QUICPacket &p){};
  // callback on packet receive event
  virtual void packet_lost_callback(QUICCallbackContext &, const QUICPacket &p){};
  // callback on packet receive event
  virtual void packet_recv_callback(QUICCallbackContext &, const QUICPacket &p){};
};

class QUICContext
{
public:
  QUICContext(QUICRTTProvider *rtt, QUICConnectionInfoProvider *info, QUICPacketProtectionKeyInfoProvider *key_info,
              QUICPathManager *path_manager);

  virtual ~QUICContext(){};
  virtual QUICConnectionInfoProvider *connection_info() const;
  virtual QUICConfig::scoped_config config() const;
  virtual QUICLDConfig &ld_config() const;
  virtual QUICPacketProtectionKeyInfoProvider *key_info() const;
  virtual QUICCCConfig &cc_config() const;
  virtual QUICRTTProvider *rtt_provider() const;
  virtual QUICPathManager *path_manager() const;

  // regist a callback which will be called when specifed event happen.
  void
  regist_callback(std::shared_ptr<QUICCallback> cbs)
  {
    this->_callbacks.push_back(cbs);
  }

  enum class CallbackEvent : uint8_t {
    PACKET_LOST,
    PACKET_SEND,
    PACKET_RECV,
    CONNECTION_CLOSE,
  };

  // FIXME stupid trigger should be fix in more smart way.
  void
  trigger(CallbackEvent e, const QUICPacket *p = nullptr)
  {
    QUICCallbackContext ctx;
    switch (e) {
    case CallbackEvent::PACKET_LOST:
      for (auto &&it : this->_callbacks) {
        it->packet_lost_callback(ctx, *p);
      }
      break;
    case CallbackEvent::PACKET_RECV:
      for (auto &&it : this->_callbacks) {
        it->packet_recv_callback(ctx, *p);
      }
      break;
    case CallbackEvent::PACKET_SEND:
      for (auto &&it : this->_callbacks) {
        it->packet_send_callback(ctx, *p);
      }
      break;
    case CallbackEvent::CONNECTION_CLOSE:
      for (auto &&it : this->_callbacks) {
        it->connection_close_callback(ctx);
      }
      break;
    default:
      break;
    }
  }

private:
  QUICConfig::scoped_config _config;
  QUICPacketProtectionKeyInfoProvider *_key_info = nullptr;
  QUICConnectionInfoProvider *_connection_info   = nullptr;
  QUICRTTProvider *_rtt_provider                 = nullptr;
  QUICPathManager *_path_manager                 = nullptr;

  std::unique_ptr<QUICLDConfig> _ld_config = nullptr;
  std::unique_ptr<QUICCCConfig> _cc_config = nullptr;

  std::vector<std::shared_ptr<QUICCallback>> _callbacks;
};
