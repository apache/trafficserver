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

#include "ts/List.h"
#include "ts/PriorityQueue.h"

#include "I_VConnection.h"

#include "QUICFrame.h"
#include "QUICStreamState.h"
#include "QUICFlowController.h"
#include "QUICIncomingFrameBuffer.h"
#include "QUICFrameGenerator.h"

class QUICNetVConnection;
class QUICStreamState;
class QUICStreamManager;

/**
 * @brief QUIC Stream
 * TODO: This is similar to Http2Stream. Need to think some integration.
 */
class QUICStream : public VConnection, public QUICFrameGenerator
{
public:
  QUICStream()
    : VConnection(nullptr), _remote_flow_controller(0, 0), _local_flow_controller(0, 0), _received_stream_frame_buffer(this)
  {
  }
  QUICStream(QUICConnectionId cid, QUICStreamId sid, uint64_t recv_max_stream_data = 0, uint64_t send_max_stream_data = 0);
  ~QUICStream();
  // void start();
  int state_stream_open(int event, void *data);
  int state_stream_closed(int event, void *data);

  void init_flow_control_params(uint32_t recv_max_stream_data, uint32_t send_max_stream_data);

  QUICStreamId id() const;
  QUICOffset final_offset();
  void reset_send_offset();
  void reset_recv_offset();
  QUICStreamFrameUPtr generete_frame(uint16_t flow_control_credit, uint16_t maximum_frame_size);

  // Implement VConnection Interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;
  void set_read_vio_nbytes(int64_t);
  void set_write_vio_nbytes(int64_t);

  QUICErrorUPtr recv(const std::shared_ptr<const QUICStreamFrame> frame);
  QUICErrorUPtr recv(const std::shared_ptr<const QUICMaxStreamDataFrame> frame);
  QUICErrorUPtr recv(const std::shared_ptr<const QUICStreamBlockedFrame> frame);

  void reset(QUICStreamErrorUPtr error);
  void shutdown();

  size_t nbytes_to_read();

  QUICOffset largest_offset_received();
  QUICOffset largest_offset_sent();

  LINK(QUICStream, link);

  // QUICFrameGenerator
  QUICFrameUPtr generate_frame(uint16_t connection_credit, uint16_t maximum_frame_size) override;
  bool will_generate_frame() override;

private:
  virtual int64_t _process_read_vio();
  virtual int64_t _process_write_vio();
  void _signal_read_event();
  void _signal_write_event();
  Event *_send_tracked_event(Event *, int, VIO *);

  void _write_to_read_vio(const std::shared_ptr<const QUICStreamFrame> &);

  QUICStreamState _state;
  bool _fin                         = false;
  QUICStreamErrorUPtr _reset_reason = nullptr;
  QUICConnectionId _connection_id   = 0;
  QUICStreamId _id                  = 0;
  QUICOffset _send_offset           = 0;

  QUICRemoteStreamFlowController _remote_flow_controller;
  QUICLocalStreamFlowController _local_flow_controller;
  uint64_t _flow_control_buffer_size = 1024;

  VIO _read_vio;
  VIO _write_vio;

  Event *_read_event  = nullptr;
  Event *_write_event = nullptr;

  // Fragments of received STREAM frame (offset is unmatched)
  // TODO: Consider to replace with ts/RbTree.h or other data structure
  QUICIncomingFrameBuffer _received_stream_frame_buffer;
};
