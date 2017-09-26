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

#include "QUICApplication.h"
#include "QUICStreamManager.h"
#include "QUICCongestionController.h"
#include "QUICLossDetector.h"
#include "QUICEvents.h"
#include "QUICPacketTransmitter.h"

class MockQUICStreamManager : public QUICStreamManager
{
public:
  MockQUICStreamManager() : QUICStreamManager() {}
  // Override
  virtual QUICError
  handle_frame(std::shared_ptr<const QUICFrame> f) override
  {
    ++_frameCount[static_cast<int>(f->type())];
    ++_totalFrameCount;

    return QUICError(QUICErrorClass::NONE);
  }

  // for Test
  int
  getStreamFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::STREAM)];
  }

  int
  getAckFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::ACK)];
  }

  int
  getPingFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::PING)];
  }

  int
  getTotalFrameCount()
  {
    return _totalFrameCount;
  }

private:
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
};

class MockNetVConnection : public NetVConnection
{
public:
  MockNetVConnection(NetVConnectionContext_t context = NET_VCONNECTION_OUT) : NetVConnection() { netvc_context = context; }
  VIO *
  do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
  {
    return nullptr;
  };
  VIO *
  do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false)
  {
    return nullptr;
  };
  void do_io_close(int lerrno = -1){};
  void do_io_shutdown(ShutdownHowTo_t howto){};
  void reenable(VIO *vio){};
  void reenable_re(VIO *vio){};
  void set_active_timeout(ink_hrtime timeout_in){};
  void set_inactivity_timeout(ink_hrtime timeout_in){};
  void cancel_active_timeout(){};
  void cancel_inactivity_timeout(){};
  void add_to_keep_alive_queue(){};
  void remove_from_keep_alive_queue(){};
  bool
  add_to_active_queue()
  {
    return true;
  };
  ink_hrtime
  get_active_timeout()
  {
    return 0;
  }
  ink_hrtime
  get_inactivity_timeout()
  {
    return 0;
  }
  void
  apply_options()
  {
  }
  SOCKET
  get_socket() { return 0; }
  int
  set_tcp_init_cwnd(int init_cwnd)
  {
    return 0;
  }
  int
  set_tcp_congestion_control(int side)
  {
    return 0;
  }
  void set_local_addr(){};
  void set_remote_addr(){};

  NetVConnectionContext_t
  get_context() const
  {
    return netvc_context;
  }
};

class MockQUICConnection : public QUICConnection
{
public:
  MockQUICConnection(NetVConnectionContext_t context = NET_VCONNECTION_OUT) : QUICConnection(), _direction(context)
  {
    this->_mutex = new_ProxyMutex();
  };
  void
  transmit_packet(std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet) override
  {
    ++_transmit_count;
  }

  void
  retransmit_packet(const QUICPacket &packet) override
  {
    ++_retransmit_count;
  }

  Ptr<ProxyMutex>
  get_packet_transmitter_mutex() override
  {
    return this->_mutex;
  }

  void
  transmit_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame) override
  {
  }

  std::vector<QUICFrameType>
  interests() override
  {
    return {QUICFrameType::CONNECTION_CLOSE};
  }

  QUICError
  handle_frame(std::shared_ptr<const QUICFrame> f) override
  {
    ++_frameCount[static_cast<int>(f->type())];
    ++_totalFrameCount;

    return QUICError(QUICErrorClass::NONE);
  }

  uint32_t
  minimum_quic_packet_size() override
  {
    return 1200;
  }

  uint32_t
  maximum_quic_packet_size() override
  {
    return 1200;
  }

  uint32_t
  maximum_stream_frame_data_size() override
  {
    return 1160;
  }

  uint32_t
  pmtu() override
  {
    return 1280;
  }

  QUICPacketNumber
  largest_received_packet_number() override
  {
    return 0;
  }

  QUICPacketNumber
  largest_acked_packet_number() override
  {
    return 0;
  }

  NetVConnectionContext_t
  direction() override
  {
    return _direction;
  }

  SSLNextProtocolSet *
  next_protocol_set() override
  {
    return nullptr;
  }

  void
  close(QUICError error) override
  {
  }

  int
  getTotalFrameCount()
  {
    return _totalFrameCount;
  }

  QUICStreamManager *
  stream_manager() override
  {
    return &_stream_manager;
  }

  int _transmit_count   = 0;
  int _retransmit_count = 0;
  Ptr<ProxyMutex> _mutex;
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
  MockQUICStreamManager _stream_manager;

  QUICTransportParametersInEncryptedExtensions dummy_transport_parameters;
  NetVConnectionContext_t _direction;
};

class MockQUICPacketTransmitter : public QUICPacketTransmitter
{
public:
  MockQUICPacketTransmitter() : QUICPacketTransmitter() { this->_mutex = new_ProxyMutex(); };
  void
  transmit_packet(std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet) override
  {
    ++_transmit_count;
  }

  void
  retransmit_packet(const QUICPacket &packet) override
  {
    ++_retransmit_count;
  }

  Ptr<ProxyMutex>
  get_packet_transmitter_mutex() override
  {
    return this->_mutex;
  }

  int _transmit_count   = 0;
  int _retransmit_count = 0;
  Ptr<ProxyMutex> _mutex;
};

class MockQUICFrameTransmitter : public QUICFrameTransmitter
{
public:
  void
  transmit_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame) override
  {
    ++frameCount[static_cast<int>(frame->type())];
  }

  uint32_t
  maximum_stream_frame_data_size() override
  {
    return 1200;
  }

  int frameCount[256] = {0};
};

class MockQUICLossDetector : public QUICLossDetector
{
public:
  MockQUICLossDetector() : QUICLossDetector(new MockQUICPacketTransmitter()) {}
  void
  rcv_frame(std::shared_ptr<const QUICFrame>)
  {
  }

  void
  on_packet_sent(std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet)
  {
  }
};

class MockQUICCongestionController : public QUICCongestionController
{
public:
  MockQUICCongestionController() : QUICCongestionController() {}
  // Override
  virtual QUICError
  handle_frame(std::shared_ptr<const QUICFrame> f) override
  {
    ++_frameCount[static_cast<int>(f->type())];
    ++_totalFrameCount;

    return QUICError(QUICErrorClass::NONE);
  }

  // for Test
  int
  getStreamFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::STREAM)];
  }

  int
  getAckFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::ACK)];
  }

  int
  getPingFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::PING)];
  }

  int
  getTotalFrameCount()
  {
    return _totalFrameCount;
  }

private:
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
};

class MockQUICApplication : public QUICApplication
{
public:
  MockQUICApplication() : QUICApplication(new MockQUICConnection) { SET_HANDLER(&MockQUICApplication::main_event_handler); }

  int
  main_event_handler(int event, Event *data)
  {
    if (event == 12345) {
      QUICStreamIO *stream_io = static_cast<QUICStreamIO *>(data->cookie);
      stream_io->write_reenable();
    }
    return EVENT_CONT;
  }

  void
  send(const uint8_t *data, size_t size, QUICStreamId stream_id)
  {
    QUICStreamIO *stream_io = this->_find_stream_io(stream_id);
    stream_io->write(data, size);
    eventProcessor.schedule_imm(this, ET_CALL, 12345, stream_io);
  }
};

void NetVConnection::cancel_OOB(){};
Action *
NetVConnection::send_OOB(Continuation *, char *, int)
{
  return nullptr;
}
