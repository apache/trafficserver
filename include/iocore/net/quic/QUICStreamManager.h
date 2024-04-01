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

#include "iocore/net/quic/QUICTypes.h"
#include "iocore/net/quic/QUICApplicationMap.h"
#include "iocore/net/quic/QUICContext.h"

class QUICTransportParameters;

class QUICStreamManager : public QUICStreamStateListener
{
public:
  QUICStreamManager(QUICContext *context, QUICApplicationMap *app_map);
  virtual ~QUICStreamManager();

  void init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                const std::shared_ptr<const QUICTransportParameters> &remote_tp);
  void set_max_streams_bidi(uint64_t max_streams);
  void set_max_streams_uni(uint64_t max_streams);
  uint64_t total_reordered_bytes() const;
  uint64_t total_offset_received() const;
  uint64_t total_offset_sent() const;

  uint32_t stream_count() const;
  QUICStream *find_stream(QUICStreamId stream_id);

  QUICConnectionErrorUPtr create_stream(QUICStreamId stream_id);
  QUICConnectionErrorUPtr create_uni_stream(QUICStreamId new_stream_id);
  QUICConnectionErrorUPtr create_bidi_stream(QUICStreamId new_stream_id);
  QUICConnectionErrorUPtr delete_stream(QUICStreamId new_stream_id);
  void reset_stream(QUICStreamId stream_id, QUICStreamErrorUPtr error);

  void set_default_application(QUICApplication *app);

  // QUICStreamStateListener
  void on_stream_state_close(const QUICStream *stream) override;

  DLL<QUICStream> stream_list;

protected:
  QUICContext *_context        = nullptr;
  QUICApplicationMap *_app_map = nullptr;
};
