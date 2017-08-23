/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "QUICTypes.h"
#include "QUICStream.h"
#include "QUICApplicationMap.h"
#include "QUICFrameHandler.h"
#include "QUICFrame.h"
#include "QUICFrameTransmitter.h"

class QUICTransportParameters;

class QUICStreamManager : public QUICFrameHandler
{
public:
  QUICStreamManager(){};

  int init(QUICFrameTransmitter *tx, QUICConnection *qc, QUICApplicationMap *app_map);
  virtual std::vector<QUICFrameType> interests() override;
  virtual void handle_frame(std::shared_ptr<const QUICFrame>) override;
  virtual void send_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame);
  void send_stream_frame(std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc> frame);
  virtual bool is_send_avail_more_than(uint64_t size);
  virtual bool is_recv_avail_more_than(uint64_t size);
  void add_recv_total_offset(uint64_t delta);
  void slide_recv_max_data();
  void init_flow_control_params(const QUICTransportParameters &local_tp, const QUICTransportParameters &remote_tp);
  uint64_t recv_max_data() const;
  uint64_t send_max_data() const;
  uint64_t recv_total_offset() const;
  uint64_t send_total_offset() const;

  DLL<QUICStream> stream_list;

private:
  QUICStream *_find_or_create_stream(QUICStreamId stream_id);
  QUICStream *_find_stream(QUICStreamId id);
  QUICError _handle_max_data_frame(std::shared_ptr<const QUICMaxDataFrame>);
  QUICError _handle_stream_frame(std::shared_ptr<const QUICStreamFrame>);
  QUICError _handle_max_stream_data_frame(std::shared_ptr<const QUICMaxStreamDataFrame>);
  QUICError _handle_stream_blocked_frame(std::shared_ptr<const QUICStreamBlockedFrame>);

  QUICApplicationMap *_app_map = nullptr;
  QUICFrameTransmitter *_tx    = nullptr;
  QUICConnection *_qc          = nullptr;

  QUICMaximumData _recv_max_data = {0};
  QUICMaximumData _send_max_data = {0};

  // TODO: Maximum Data is in units of 1024 octets, but those total offset are in units of octets.
  // Add new uint16_t fields for remainder and treat those total offset in units of 1024 octets if needed
  uint64_t _recv_total_offset = 0;
  uint64_t _send_total_offset = 0;
};
