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
#include "QUICLossDetector.h"

extern ClassAllocator<QUICBidirectionalStream> quicStreamAllocator;

class QUICTransportParameters;

class QUICStreamManager : public QUICFrameHandler, public QUICFrameGenerator
{
public:
  QUICStreamManager(){};
  QUICStreamManager(QUICConnectionInfoProvider *info, QUICRTTProvider *rtt_provider, QUICApplicationMap *app_map);

  void init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                const std::shared_ptr<const QUICTransportParameters> &remote_tp);
  void set_max_streams_bidi(uint64_t max_streams);
  void set_max_streams_uni(uint64_t max_streams);
  uint64_t total_reordered_bytes() const;
  uint64_t total_offset_received() const;
  uint64_t total_offset_sent() const;
  void add_total_offset_sent(uint32_t sent_byte);

  uint32_t stream_count() const;
  QUICConnectionErrorUPtr create_stream(QUICStreamId stream_id);
  QUICConnectionErrorUPtr create_uni_stream(QUICStreamId &new_stream_id);
  QUICConnectionErrorUPtr create_bidi_stream(QUICStreamId &new_stream_id);
  void reset_stream(QUICStreamId stream_id, QUICStreamErrorUPtr error);

  void set_default_application(QUICApplication *app);

  DLL<QUICStream> stream_list;

  // QUICFrameHandler
  virtual std::vector<QUICFrameType> interests() override;
  virtual QUICConnectionErrorUPtr handle_frame(QUICEncryptionLevel level, const QUICFrame &frame) override;

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            ink_hrtime timestamp) override;

private:
  QUICStream *_find_stream(QUICStreamId id);
  QUICStream *_find_or_create_stream(QUICStreamId stream_id);
  QUICConnectionErrorUPtr _handle_frame(const QUICStreamFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICRstStreamFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICStopSendingFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICMaxStreamDataFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICStreamDataBlockedFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICMaxStreamsFrame &frame);
  std::vector<QUICEncryptionLevel>
  _encryption_level_filter() override
  {
    return {
      QUICEncryptionLevel::ZERO_RTT,
      QUICEncryptionLevel::ONE_RTT,
    };
  }

  QUICConnectionInfoProvider *_info                         = nullptr;
  QUICRTTProvider *_rtt_provider                            = nullptr;
  QUICApplicationMap *_app_map                              = nullptr;
  std::shared_ptr<const QUICTransportParameters> _local_tp  = nullptr;
  std::shared_ptr<const QUICTransportParameters> _remote_tp = nullptr;
  QUICStreamId _local_max_streams_bidi                      = 0;
  QUICStreamId _local_max_streams_uni                       = 0;
  QUICStreamId _remote_max_streams_bidi                     = 0;
  QUICStreamId _remote_max_streams_uni                      = 0;
  QUICStreamId _next_stream_id_uni                          = 0;
  QUICStreamId _next_stream_id_bidi                         = 0;
  uint64_t _total_offset_sent                               = 0;
};
