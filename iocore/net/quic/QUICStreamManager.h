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

extern ClassAllocator<QUICStream> quicStreamAllocator;

class QUICTransportParameters;

class QUICStreamManager : public QUICFrameHandler, public QUICFrameGenerator
{
public:
  QUICStreamManager(){};
  QUICStreamManager(QUICConnectionId cid, QUICApplicationMap *app_map);

  void init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                const std::shared_ptr<const QUICTransportParameters> &remote_tp);
  void set_max_stream_id(QUICStreamId id);
  uint64_t total_offset_received() const;
  uint64_t total_offset_sent() const;
  void add_total_offset_sent(uint32_t sent_byte);

  uint32_t stream_count() const;
  QUICErrorUPtr create_stream(QUICStreamId stream_id);

  void set_default_application(QUICApplication *app);
  void reset_send_offset();
  void reset_recv_offset();

  DLL<QUICStream> stream_list;

  // QUICFrameHandler
  virtual std::vector<QUICFrameType> interests() override;
  virtual QUICErrorUPtr handle_frame(std::shared_ptr<const QUICFrame>) override;

  // QUICFrameGenerator
  bool will_generate_frame() override;
  QUICFrameUPtr generate_frame(uint16_t connection_credit, uint16_t maximum_frame_size) override;

private:
  QUICStream *_find_stream(QUICStreamId id);
  QUICStream *_find_or_create_stream(QUICStreamId stream_id);
  QUICErrorUPtr _handle_frame(const std::shared_ptr<const QUICStreamFrame> &);
  QUICErrorUPtr _handle_frame(const std::shared_ptr<const QUICRstStreamFrame> &);
  QUICErrorUPtr _handle_frame(const std::shared_ptr<const QUICStopSendingFrame> &);
  QUICErrorUPtr _handle_frame(const std::shared_ptr<const QUICMaxStreamDataFrame> &);
  QUICErrorUPtr _handle_frame(const std::shared_ptr<const QUICStreamBlockedFrame> &);
  QUICErrorUPtr _handle_frame(const std::shared_ptr<const QUICMaxStreamIdFrame> &);

  QUICConnectionId _connection_id                           = 0;
  QUICApplicationMap *_app_map                              = nullptr;
  std::shared_ptr<const QUICTransportParameters> _local_tp  = nullptr;
  std::shared_ptr<const QUICTransportParameters> _remote_tp = nullptr;
  QUICStreamId _local_maximum_stream_id_bidi                = 0;
  QUICStreamId _local_maximum_stream_id_uni                 = 0;
  QUICStreamId _remote_maximum_stream_id_bidi               = 0;
  QUICStreamId _remote_maximum_stream_id_uni                = 0;
  uint64_t _total_offset_sent                               = 0;
};
