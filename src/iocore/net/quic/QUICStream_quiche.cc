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

#include "iocore/net/quic/QUICStream_quiche.h"
#include "iocore/net/quic/QUICStreamAdapter.h"

QUICStreamImpl::QUICStreamImpl() {}

QUICStreamImpl::QUICStreamImpl(QUICConnectionInfoProvider *cinfo, QUICStreamId sid) : QUICStream(cinfo, sid) {}

QUICOffset
QUICStreamImpl::final_offset() const
{
  return 0;
}

void
QUICStreamImpl::stop_sending(QUICStreamErrorUPtr error)
{
}

void
QUICStreamImpl::reset(QUICStreamErrorUPtr error)
{
}

void
QUICStreamImpl::on_read()
{
}

void
QUICStreamImpl::on_eos()
{
}

void
QUICStreamImpl::receive_data(quiche_conn *quiche_con)
{
  uint8_t buf[4096];
  bool fin;
  ssize_t read_len = 0;

  while ((read_len = quiche_conn_stream_recv(quiche_con, this->_id, buf, sizeof(buf), &fin)) > 0) {
    this->_adapter->write(this->_received_bytes, buf, read_len, fin);
    this->_received_bytes += read_len;
  }

  this->_adapter->encourge_read();
}

void
QUICStreamImpl::send_data(quiche_conn *quiche_con)
{
  bool fin    = false;
  ssize_t len = 0;

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
      quiche_conn_stream_send(quiche_con, this->_id, reinterpret_cast<uint8_t *>(block->start()), block->size(), fin);
    if (written_len >= 0) {
      this->_sent_bytes += written_len;
    }
  }
  this->_adapter->encourge_write();
}
