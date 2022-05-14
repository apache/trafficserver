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
#include "QUICStreamAdapter.h"
#include "I_IOBuffer.h"

class QUICStreamVCAdapter : public VConnection, public QUICStreamAdapter
{
public:
  class IOInfo;

  QUICStreamVCAdapter(QUICStream &stream);
  virtual ~QUICStreamVCAdapter();

  // Implement QUICStreamAdapter Interface
  int64_t write(QUICOffset offset, const uint8_t *data, uint64_t data_length, bool fin) override;
  void encourge_read() override;
  bool is_eos() override;
  uint64_t unread_len() override;
  uint64_t read_len() override;
  uint64_t total_len() override;
  void encourge_write() override;
  void notify_eos() override;

  // Implement VConnection Interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  int state_stream_open(int event, void *data);
  int state_stream_closed(int event, void *data);

protected:
  Ptr<IOBufferBlock> _read(size_t len) override;

  VIO _read_vio;
  VIO _write_vio;

  Event *_read_event  = nullptr;
  Event *_write_event = nullptr;
};

class QUICStreamVCAdapter::IOInfo
{
public:
  IOInfo(QUICStream &stream)
    : adapter(stream), read_buffer(new_MIOBuffer(BUFFER_SIZE_INDEX_8K)), write_buffer(new_MIOBuffer(BUFFER_SIZE_INDEX_8K))
  {
  }
  ~IOInfo()
  {
    free_MIOBuffer(this->read_buffer);
    free_MIOBuffer(this->write_buffer);
  }

  void
  setup_read_vio(Continuation *c)
  {
    read_vio = adapter.do_io_read(c, INT64_MAX, read_buffer);

    // This is uncommon but it has basically the same effect as
    // read_buffer->alloc_reader, and it allows VIO user to obtain the
    // reader by calling read_vio.get_reader()
    // It limits a number of readers to one, but it wouldn't be a real
    // limitation for this particular usecase in QUICStreamVCAdapter.
    read_vio->set_reader(read_buffer->alloc_reader());
    adapter.encourge_read();
  }

  void
  setup_write_vio(Continuation *c)
  {
    write_vio = adapter.do_io_write(c, INT64_MAX, write_buffer->alloc_reader());
    adapter.encourge_write();
  }

  QUICStreamVCAdapter adapter;
  MIOBuffer *read_buffer;
  MIOBuffer *write_buffer;
  VIO *read_vio  = nullptr;
  VIO *write_vio = nullptr;
};
