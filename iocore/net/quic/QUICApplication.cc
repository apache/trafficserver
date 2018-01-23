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
#include "QUICStream.h"

static constexpr char tag[] = "quic_app";

//
// QUICStreamIO
//
QUICStreamIO::QUICStreamIO(QUICApplication *app, QUICStream *stream) : _stream(stream)
{
  this->_read_buffer  = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
  this->_write_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);

  this->_read_buffer_reader  = _read_buffer->alloc_reader();
  this->_write_buffer_reader = _write_buffer->alloc_reader();

  this->_read_vio  = stream->do_io_read(app, INT64_MAX, this->_read_buffer);
  this->_write_vio = stream->do_io_write(app, INT64_MAX, this->_write_buffer_reader);
}

int64_t
QUICStreamIO::read_avail()
{
  return this->_read_buffer_reader->read_avail();
}

bool
QUICStreamIO::is_read_avail_more_than(int64_t size)
{
  return this->_read_buffer_reader->is_read_avail_more_than(size);
}

int64_t
QUICStreamIO::read(uint8_t *buf, int64_t len)
{
  int64_t read_len = this->_read_buffer_reader->read(const_cast<uint8_t *>(buf), len);
  this->_read_vio->ndone += read_len;
  return read_len;
}

int64_t
QUICStreamIO::write(const uint8_t *buf, int64_t len)
{
  SCOPED_MUTEX_LOCK(lock, this->_write_vio->mutex, this_ethread());

  return this->_write_buffer->write(buf, len);
}

int64_t
QUICStreamIO::write(IOBufferReader *r, int64_t alen, int64_t offset)
{
  SCOPED_MUTEX_LOCK(lock, this->_write_vio->mutex, this_ethread());

  int64_t bytes_avail = this->_write_buffer->write_avail();
  Debug(tag, "nbytes=%" PRId64 " ndone=%" PRId64 " write_avail=%" PRId64 " write_len=%" PRId64, this->_write_vio->nbytes,
        this->_write_vio->ndone, bytes_avail, alen);

  if (bytes_avail > 0) {
    int64_t len = std::min(bytes_avail, alen);
    return this->_write_buffer->write(r, len, offset);
  } else {
    return 0;
  }
}

void
QUICStreamIO::set_write_vio_nbytes(int64_t nbytes)
{
  this->_write_vio->nbytes = nbytes;
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

IOBufferReader *
QUICStreamIO::get_read_buffer_reader()
{
  return this->_read_buffer_reader;
}

void
QUICStreamIO::shutdown()
{
  return this->_stream->shutdown();
}

uint32_t
QUICStreamIO::get_transaction_id() const
{
  return this->_stream->id();
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
QUICApplication::set_stream(QUICStream *stream, QUICStreamIO *stream_io)
{
  if (stream_io == nullptr) {
    stream_io = new QUICStreamIO(this, stream);
  }
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
    Debug(tag, "[%" PRIx64 "] Unknown Stream, id: %" PRIx64, static_cast<uint64_t>(this->_client_qc->connection_id()),
          stream->id());
  }

  return;
}

void
QUICApplication::unset_stream(QUICStream *stream)
{
  QUICStreamIO *stream_io = this->_find_stream_io(stream->id());
  if (stream_io) {
    this->_stream_map.erase(stream->id());
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

QUICStreamIO *
QUICApplication::_find_stream_io(VIO *vio)
{
  if (vio == nullptr) {
    return nullptr;
  }

  QUICStream *stream = dynamic_cast<QUICStream *>(vio->vc_server);
  if (stream == nullptr) {
    return nullptr;
  }

  return this->_find_stream_io(stream->id());
}
