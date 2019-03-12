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

#include "QUICStream.h"

class QUICSendStream : public QUICStreamVConnection
{
public:
  QUICSendStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid, uint64_t send_max_stream_data);
  QUICSendStream() : _remote_flow_controller(0, 0), _state(nullptr, nullptr) {}
  int state_stream_open(int event, void *data);
  int state_stream_closed(int event, void *data);

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            ink_hrtime timestamp) override;

  virtual QUICConnectionErrorUPtr recv(const QUICMaxStreamDataFrame &frame) override;
  virtual QUICConnectionErrorUPtr recv(const QUICStopSendingFrame &frame) override;

  // Implement VConnection Interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  void reset(QUICStreamErrorUPtr error) override;

private:
  QUICStreamErrorUPtr _reset_reason = nullptr;
  bool _is_reset_sent               = false;

  bool _is_transfer_complete = false;
  bool _is_reset_complete    = false;

  QUICTransferProgressProviderVIO _progress_vio = {this->_read_vio};

  QUICRemoteStreamFlowController _remote_flow_controller;

  QUICSendStreamStateMachine _state;

  // QUICFrameGenerator
  void _on_frame_acked(QUICFrameInformationUPtr &info) override;
  void _on_frame_lost(QUICFrameInformationUPtr &info) override;
};
