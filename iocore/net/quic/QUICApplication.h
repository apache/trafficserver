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

#include "I_EventSystem.h"
#include "I_IOBuffer.h"
#include "QUICTypes.h"
#include "QUICConnection.h"
#include "QUICStream.h"

class QUICApplication;

/**
 @brief QUICStream I/O Interface for QUICApplication
 */
class QUICStreamIO
{
public:
  QUICStreamIO(QUICApplication *app, QUICStreamVConnection *stream);
  virtual ~QUICStreamIO();

  uint32_t stream_id() const;
  bool is_bidirectional() const;

  int64_t read(uint8_t *buf, int64_t len);
  int64_t peek(uint8_t *buf, int64_t len);
  void consume(int64_t len);
  bool is_read_done() const;
  virtual void read_reenable();

  int64_t write(const uint8_t *buf, int64_t len);
  int64_t write(IOBufferReader *r, int64_t len);
  int64_t write(IOBufferBlock *b);
  void write_done();
  virtual void write_reenable();

protected:
  MIOBuffer *_read_buffer  = nullptr;
  MIOBuffer *_write_buffer = nullptr;

  IOBufferReader *_read_buffer_reader  = nullptr;
  IOBufferReader *_write_buffer_reader = nullptr;

private:
  QUICStreamVConnection *_stream_vc = nullptr;

  VIO *_read_vio  = nullptr;
  VIO *_write_vio = nullptr;

  // Track how much data is written to _write_vio. When total size of data become clear,
  // set it to _write_vio.nbytes.
  uint64_t _nwritten = 0;
};

/**
 * @brief Abstract QUIC Application Class
 * @detail Every quic application must inherits this class
 */
class QUICApplication : public Continuation
{
public:
  QUICApplication(QUICConnection *qc);
  virtual ~QUICApplication();

  void set_stream(QUICStreamVConnection *stream_vc, QUICStreamIO *stream_io = nullptr);
  void set_stream(QUICStreamIO *stream_io);
  bool is_stream_set(QUICStreamVConnection *stream_vc);
  void reenable(QUICStreamVConnection *stream_vc);
  void unset_stream(QUICStreamVConnection *stream_vc);

protected:
  QUICStreamIO *_find_stream_io(QUICStreamId id);
  QUICStreamIO *_find_stream_io(VIO *vio);

  QUICConnection *_qc = nullptr;

private:
  std::map<QUICStreamId, QUICStreamIO *> _stream_map;
};
