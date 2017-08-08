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

#include "I_VConnection.h"
#include "QUICTypes.h"

class QUICNetVConnection;
class QUICStream;
class QUICApplication;

/**
 * @brief QUICStream I/O interface from QUIC Application
 */
class QUICStreamIO
{
public:
  QUICStreamIO(QUICApplication *app, QUICStream *stream);

  int64_t read_avail();
  int64_t read(uint8_t *buf, int64_t len);
  int64_t write(uint8_t *buf, int64_t len);
  void read_reenable();
  void write_reenable();

private:
  MIOBuffer *_read_buffer  = nullptr;
  MIOBuffer *_write_buffer = nullptr;

  IOBufferReader *_read_buffer_reader  = nullptr;
  IOBufferReader *_write_buffer_reader = nullptr;

  VIO *_read_vio  = nullptr;
  VIO *_write_vio = nullptr;
};

/**
 * @brief Abstruct QUIC Application Class
 * @detail Every quic application must inherits this class
 */
class QUICApplication : public Continuation
{
public:
  QUICApplication(ProxyMutex *m, QUICNetVConnection *vc);

  void set_stream(QUICStream *stream);
  bool is_stream_set(QUICStream *stream);
  void reenable(QUICStream *stream);
  void unset_stream(QUICStream *stream);

protected:
  QUICStreamIO *_find_stream_io(QUICStreamId id);

  QUICNetVConnection *_client_vc = nullptr;

private:
  std::map<QUICStreamId, QUICStreamIO *> _stream_map;
};
