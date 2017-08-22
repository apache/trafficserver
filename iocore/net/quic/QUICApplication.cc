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

#include "ts/MemView.h"
#include "QUICStream.h"

const static char *tag = "quic_app";

//
// QUICStreamIO
//
QUICStreamIO::QUICStreamIO(QUICApplication *app, QUICStream *stream)
{
  this->_read_buffer  = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  this->_write_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);

  this->_read_buffer_reader  = _read_buffer->alloc_reader();
  this->_write_buffer_reader = _write_buffer->alloc_reader();

  this->_read_vio  = stream->do_io_read(app, 0, _read_buffer);
  this->_write_vio = stream->do_io_write(app, 0, _write_buffer_reader);
}

int64_t
QUICStreamIO::read_avail()
{
  return this->_read_buffer_reader->read_avail();
}

int64_t
QUICStreamIO::read(uint8_t *buf, int64_t len)
{
  return this->_read_buffer_reader->read(const_cast<uint8_t *>(buf), len);
}

int64_t
QUICStreamIO::write(uint8_t *buf, int64_t len)
{
  SCOPED_MUTEX_LOCK(lock, this->_write_vio->mutex, this_ethread());

  int64_t bytes_add = this->_write_buffer->write(buf, len);
  this->_write_vio->nbytes += bytes_add;

  return bytes_add;
}

void
QUICStreamIO::read_reenable()
{
  return this->_read_vio->reenable();
}

void
QUICStreamIO::write_reenable()
{
  return this->_write_vio->reenable();
}

//
// QUICApplication
//
QUICApplication::QUICApplication(QUICConnection *qc) : Continuation(new_ProxyMutex())
{
  this->_client_qc = qc;
}

// @brief Bind stream and application
void
QUICApplication::set_stream(QUICStream *stream)
{
  QUICStreamIO *stream_io = new QUICStreamIO(this, stream);
  this->_stream_map.insert(std::make_pair(stream->id(), stream_io));
}

bool
QUICApplication::is_stream_set(QUICStream *stream)
{
  auto result = this->_stream_map.find(stream->id());

  return result != this->_stream_map.end();
}

void
QUICApplication::reenable(QUICStream *stream)
{
  QUICStreamIO *stream_io = this->_find_stream_io(stream->id());
  if (stream_io) {
    stream_io->read_reenable();
    stream_io->write_reenable();
  } else {
    Debug(tag, "Unknown Stream, id: %d", stream->id());
  }

  return;
}

void
QUICApplication::unset_stream(QUICStream *stream)
{
  QUICStreamIO *stream_io = this->_find_stream_io(stream->id());
  if (stream_io) {
    this->_stream_map.erase(stream->id());
    delete stream_io;
  }
}

QUICStreamIO *
QUICApplication::_find_stream_io(QUICStreamId id)
{
  auto result = this->_stream_map.find(id);

  if (result == this->_stream_map.end()) {
    return nullptr;
  } else {
    return result->second;
  }
}
