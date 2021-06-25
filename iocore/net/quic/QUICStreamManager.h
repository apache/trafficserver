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
#include "QUICBidirectionalStream.h"
#include "QUICUnidirectionalStream.h"
#include "QUICApplicationMap.h"
#include "QUICFrameHandler.h"
#include "QUICFrame.h"
#include "QUICStreamFactory.h"
#include "QUICLossDetector.h"
#include "QUICPathManager.h"
#include "QUICContext.h"

class QUICTransportParameters;

class QUICStreamManager : public QUICFrameHandler, public QUICFrameGenerator, public QUICStreamStateListener
{
public:
  QUICStreamManager(QUICContext *context, QUICApplicationMap *app_map);
  ~QUICStreamManager();

  void init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                const std::shared_ptr<const QUICTransportParameters> &remote_tp);
  void set_max_streams_bidi(uint64_t max_streams);
  void set_max_streams_uni(uint64_t max_streams);
  uint64_t total_reordered_bytes() const;
  uint64_t total_offset_received() const;
  uint64_t total_offset_sent() const;

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
  bool will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t timestamp) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            size_t current_packet_size, uint32_t timestamp) override;

  // QUICStreamStateListener
  void on_stream_state_close(const QUICStream *stream) override;

protected:
  virtual bool _is_level_matched(QUICEncryptionLevel level) override;

private:
  QUICStream *_find_stream(QUICStreamId id);
  QUICStream *_find_or_create_stream(QUICStreamId stream_id);
  void _add_total_offset_sent(uint32_t sent_byte);
  QUICConnectionErrorUPtr _handle_frame(const QUICStreamFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICRstStreamFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICStopSendingFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICMaxStreamDataFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICStreamDataBlockedFrame &frame);
  QUICConnectionErrorUPtr _handle_frame(const QUICMaxStreamsFrame &frame);

  QUICStreamFactory _stream_factory;

  QUICContext *_context                                       = nullptr;
  QUICApplicationMap *_app_map                                = nullptr;
  std::shared_ptr<const QUICTransportParameters> _local_tp    = nullptr;
  std::shared_ptr<const QUICTransportParameters> _remote_tp   = nullptr;
  QUICStreamId _local_max_streams_bidi                        = 0;
  QUICStreamId _local_max_streams_uni                         = 0;
  QUICStreamId _remote_max_streams_bidi                       = 0;
  QUICStreamId _remote_max_streams_uni                        = 0;
  QUICStreamId _next_stream_id_uni                            = 0;
  QUICStreamId _next_stream_id_bidi                           = 0;
  uint64_t _total_offset_sent                                 = 0;
  std::array<QUICEncryptionLevel, 2> _encryption_level_filter = {
    QUICEncryptionLevel::ZERO_RTT,
    QUICEncryptionLevel::ONE_RTT,
  };

  uint64_t _stream_id_to_nth_stream(QUICStreamId stream_id);
};
