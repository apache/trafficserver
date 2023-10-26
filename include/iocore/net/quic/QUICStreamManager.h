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

  virtual void init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                        const std::shared_ptr<const QUICTransportParameters> &remote_tp) = 0;
  virtual void set_max_streams_bidi(uint64_t max_streams)                                                = 0;
  virtual void set_max_streams_uni(uint64_t max_streams)                                                 = 0;
  virtual uint64_t total_reordered_bytes() const                                                         = 0;
  virtual uint64_t total_offset_received() const                                                         = 0;
  virtual uint64_t total_offset_sent() const                                                             = 0;

  virtual uint32_t stream_count() const                   = 0;
  virtual QUICStream *find_stream(QUICStreamId stream_id) = 0;

  virtual QUICConnectionErrorUPtr create_stream(QUICStreamId stream_id)           = 0;
  virtual QUICConnectionErrorUPtr create_uni_stream(QUICStreamId &new_stream_id)  = 0;
  virtual QUICConnectionErrorUPtr create_bidi_stream(QUICStreamId &new_stream_id) = 0;
  virtual QUICConnectionErrorUPtr delete_stream(QUICStreamId &new_stream_id)      = 0;
  virtual void reset_stream(QUICStreamId stream_id, QUICStreamErrorUPtr error)    = 0;

  void set_default_application(QUICApplication *app);

protected:
  QUICContext *_context        = nullptr;
  QUICApplicationMap *_app_map = nullptr;
};
