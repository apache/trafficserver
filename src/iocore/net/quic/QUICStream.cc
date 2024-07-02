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

#include "iocore/net/quic/QUICStream.h"
#include "iocore/net/quic/QUICStreamManager.h"
#include "iocore/net/quic/QUICStreamAdapter.h"

constexpr uint32_t MAX_STREAM_FRAME_OVERHEAD = 24;

QUICStream::QUICStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid) : _connection_info(cinfo), _id(sid) {}

QUICStream::~QUICStream() {}

QUICStreamId
QUICStream::id() const
{
  return this->_id;
}

const QUICConnectionInfoProvider *
QUICStream::connection_info()
{
  return this->_connection_info;
}

QUICStreamDirection
QUICStream::direction() const
{
  return QUICTypeUtil::detect_stream_direction(this->_id, this->_connection_info->direction());
}

bool
QUICStream::is_bidirectional() const
{
  return ((this->_id & 0x03) < 0x02);
}

bool
QUICStream::has_no_more_data() const
{
  return this->_has_no_more_data;
}

void
QUICStream::set_io_adapter(QUICStreamAdapter *adapter)
{
  this->_adapter = adapter;
}

QUICOffset
QUICStream::final_offset() const
{
  return 0;
}

void
QUICStream::stop_sending(QUICStreamErrorUPtr /* error ATS_UNUSED */)
{
}

void
QUICStream::reset(QUICStreamErrorUPtr /* error ATS_UNUSED */)
{
}

void
QUICStream::on_read()
{
}

void
QUICStream::on_eos()
{
}

void
QUICStream::receive_data(quiche_conn *quiche_con)
{
  uint8_t                    buf[4096];
  bool                       fin;
  ssize_t                    read_len = 0;
  [[maybe_unused]] ErrorCode error_code{0}; // Only set if QUICHE_ERR_STREAM_STOPPED(-15) or QUICHE_ERR_STREAM_RESET(-16) are
                                            // returned by quiche_conn_stream_recv.

  while ((read_len = quiche_conn_stream_recv(quiche_con, this->_id, buf, sizeof(buf), &fin, &error_code)) > 0) {
    this->_adapter->write(this->_received_bytes, buf, read_len, fin);
    this->_received_bytes += read_len;
  }
  this->_has_no_more_data = quiche_conn_stream_finished(quiche_con, this->_id);

  this->_adapter->encourge_read();
}

void
QUICStream::send_data(quiche_conn *quiche_con)
{
  bool                       fin = false;
  ssize_t                    len = 0;
  [[maybe_unused]] ErrorCode error_code{0}; // Only set if QUICHE_ERR_STREAM_STOPPED(-15) or QUICHE_ERR_STREAM_RESET(-16) are
                                            // returned by quiche_conn_stream_send.

  len = quiche_conn_stream_capacity(quiche_con, this->_id);
  if (len <= 0) {
    return;
  }
  Ptr<IOBufferBlock> block = this->_adapter->read(len);
  if (this->_adapter->total_len() == this->_sent_bytes + block->size()) {
    fin = true;
  }
  if (block->size() > 0 || fin) {
    ssize_t written_len =
      quiche_conn_stream_send(quiche_con, this->_id, reinterpret_cast<uint8_t *>(block->start()), block->size(), fin, &error_code);
    if (written_len >= 0) {
      this->_sent_bytes += written_len;
    }
  }
  this->_adapter->encourge_write();
}
