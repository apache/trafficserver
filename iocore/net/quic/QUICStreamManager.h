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
  QUICStreamManager(QUICFrameTransmitter *tx, QUICApplicationMap *app_map);

  virtual void send_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame);
  virtual void send_frame(std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc> frame);
  void init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                const std::shared_ptr<const QUICTransportParameters> &remote_tp);
  uint64_t total_offset_received() const;
  uint64_t total_offset_sent() const;
  uint32_t stream_count() const;

  DLL<QUICStream> stream_list;

  // QUICFrameHandler
  virtual std::vector<QUICFrameType> interests() override;
  virtual QUICError handle_frame(std::shared_ptr<const QUICFrame>) override;

private:
  QUICStream *_find_or_create_stream(QUICStreamId stream_id);
  QUICStream *_find_stream(QUICStreamId id);
  QUICError _handle_frame(const std::shared_ptr<const QUICStreamFrame> &);
  QUICError _handle_frame(const std::shared_ptr<const QUICRstStreamFrame> &);
  QUICError _handle_frame(const std::shared_ptr<const QUICMaxStreamDataFrame> &);
  QUICError _handle_frame(const std::shared_ptr<const QUICStreamBlockedFrame> &);

  QUICFrameTransmitter *_tx                                 = nullptr;
  QUICApplicationMap *_app_map                              = nullptr;
  std::shared_ptr<const QUICTransportParameters> _local_tp  = nullptr;
  std::shared_ptr<const QUICTransportParameters> _remote_tp = nullptr;

  QUICMaximumData _recv_max_data = {0};
  QUICMaximumData _send_max_data = {0};
};
